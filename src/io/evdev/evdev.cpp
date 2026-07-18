#include "../internal.hpp"

#include <core/log.hpp>
#include <core/fd.hpp>

struct IoEvdevDevice : IoInputDeviceBase
{
    Fd fd = {};
    struct libevdev* evdev = nullptr;
    bool needs_sync;

    ~IoEvdevDevice()
    {
        debug_assert(!fd && !evdev);
    }

    void destroy(IoContext* io)
    {
        libevdev_free(std::exchange(evdev, nullptr));
        fd_unlisten(io->exec, fd.get());
        fd.reset();
    }
};

struct IoEvdev
{
    RefVector<IoEvdevDevice> devices;

    struct udev_monitor* monitor;
};

static
void send_event(IoEvdevDevice* device, const input_event& event, bool quiet)
{
    switch (event.type) {
        break;case EV_KEY:
              case EV_REL:
            io_input_device_post(device, false, {{{event.type, event.code, num_cast<f64>(event.value)}}});
        break;case EV_ABS: {
            auto* info = libevdev_get_abs_info(device->evdev, event.code);
            f64 range = info->maximum - info->minimum;
            f64 progress = event.value - info->minimum;
            io_input_device_post(device, quiet, {{{event.type, event.code, (progress / range) * 2 - 1}}});
        }
    }
}

static
void handle_evdev_event(IoContext* io, IoEvdevDevice* device)
{
    input_event event;
    for (;;) {
        auto res = unix_check<libevdev_next_event, EAGAIN, ENODEV>(device->evdev,
            device->needs_sync
                ? LIBEVDEV_READ_FLAG_SYNC
                : LIBEVDEV_READ_FLAG_NORMAL, &event);

        if (res.value == LIBEVDEV_READ_STATUS_SYNC) {
            device->needs_sync = true;
            if (event.type == EV_SYN && event.code == SYN_DROPPED) {
                log_debug("evdev :: sync required");
                continue;
            } else {
                send_event(device, event, true);
            }
        } else if (res.error == EAGAIN) {
            if (device->needs_sync) {
                log_debug("evdev :: sync completed!");
                device->needs_sync = false;
            }
            return;
        } else if (res.error == ENODEV) {
            log_debug("evdev :: device disconnected");
            device->destroy(io);
            io->evdev->devices.erase(device);
            return;
        } else {
            send_event(device, event, false);
        }
    }
}

static
void handle_device(IoContext* io, udev_device* dev)
{
    auto devnode = udev_device_get_devnode(dev);
    if (!devnode) return;

    auto fd = Fd(unix_check<open, EACCES>(devnode, O_RDONLY | O_NONBLOCK).value);
    if (!fd) return;

    libevdev* evdev = nullptr;
    if (unix_check<libevdev_new_from_fd, ENOTTY, EINVAL>(fd.get(), &evdev).err()) return;
    defer { libevdev_free(evdev); };

    log_debug("evdev = {}", libevdev_get_name(evdev));
    log_debug("  vid = {:#06x}", libevdev_get_id_vendor(evdev));
    log_debug("  pid = {:#06x}", libevdev_get_id_product(evdev));

    auto device = ref_create<IoEvdevDevice>();
    device->io    = io;
    device->fd    = std::move(fd);
    device->evdev = std::exchange(evdev, nullptr);
    io->evdev->devices.emplace_back(device.get());

    io_input_device_add(device.get());

    fd_listen(io->exec, device->fd.get(), FdEventBit::readable, [io, device = device.get()](fd_t, Flags<FdEventBit>) {
        handle_evdev_event(io, device);
    });
}

static
void handle_monitor_events(IoContext* io)
{
    for (;;) {
        auto dev = udev_monitor_receive_device(io->evdev->monitor);
        if (!dev) break;
        defer { udev_device_unref(dev); };

        auto action = udev_device_get_action(dev);
        if ("add"sv == action) handle_device(io, dev);
    }
}

void io_evdev_init(IoContext* io)
{
    io->evdev = ref_create<IoEvdev>();

    auto enumerate = udev_enumerate_new(io->udev);
    defer { udev_enumerate_unref(enumerate); };

    udev_enumerate_add_match_subsystem(enumerate, "input");
    udev_enumerate_scan_devices(enumerate);

    auto devices = udev_enumerate_get_list_entry(enumerate);
    udev_list_entry* entry;
    udev_list_entry_foreach(entry, devices) {
        auto syspath = udev_list_entry_get_name(entry);
        auto dev = udev_device_new_from_syspath(io->udev, syspath);
        defer { udev_device_unref(dev); };

        handle_device(io, dev);
    }

    io->evdev->monitor = udev_monitor_new_from_netlink(io->udev, "udev");
    udev_monitor_filter_add_match_subsystem_devtype(io->evdev->monitor, "input", nullptr);
    udev_monitor_enable_receiving(io->evdev->monitor);
    auto fd = udev_monitor_get_fd(io->evdev->monitor);
    fd_listen(io->exec, fd, FdEventBit::readable, [io](fd_t, Flags<FdEventBit>) {
        handle_monitor_events(io);
    });
}

void io_evdev_deinit(IoContext* io)
{
    for (auto& device : io->evdev->devices) {
        device->destroy(io);
    }

    fd_unlisten(io->exec, udev_monitor_get_fd(io->evdev->monitor));
    udev_monitor_unref(io->evdev->monitor);

    io->evdev.destroy();
}
