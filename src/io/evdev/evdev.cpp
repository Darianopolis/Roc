#include "../internal.hpp"

struct IoEvdevDevice
{
    int fd = -1;
    struct libevdev* evdev = nullptr;
    bool needs_sync;

    ~IoEvdevDevice()
    {
        debug_assert(!fd_is_valid(fd) && !evdev);
    }

    void destroy(IoContext* ctx)
    {
        libevdev_free(std::exchange(evdev, nullptr));
        exec_fd_unlisten(ctx->exec, fd);
        close(std::exchange(fd, -1));
    }
};

struct IoEvdev
{
    RefVector<IoEvdevDevice> devices;

    struct udev_monitor* monitor;
};

static
void handle_evdev_event(IoContext* ctx, IoEvdevDevice* device)
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
                log_debug("Sync required");
                continue;
            } else {
                log_debug("Sync ({}) = {}", libevdev_event_code_get_name(event.type, event.code), event.value);
            }
        } else if (res.error == EAGAIN) {
            if (device->needs_sync) {
                log_debug("Sync completed!");
                device->needs_sync = false;
            }
            return;
        } else if (res.error == ENODEV) {
            log_debug("Device disconnected");
            device->destroy(ctx);
            ctx->evdev->devices.erase(device);
            return;
        } else {
            log_trace("Event ({}) = {}", libevdev_event_code_get_name(event.type, event.code), event.value);

            if (event.type == EV_KEY && event.code == BTN_MODE && event.value == 1) {
                log_error("EMERGENCY SHUTDOWN");
                debug_kill();
            }
        }
    }
}

static
void handle_device(IoContext* ctx, udev_device* dev)
{
    auto devnode = udev_device_get_devnode(dev);
    if (!devnode) return;

    auto fd = open(devnode, O_RDONLY | O_NONBLOCK);
    if (fd < 0) return;
    defer { close(fd); };

    libevdev* evdev = nullptr;
    if (unix_check<libevdev_new_from_fd, ENOTTY, EINVAL>(fd, &evdev).err()) return;
    defer { libevdev_free(evdev); };

    log_debug("evdev = {}", libevdev_get_name(evdev));
    log_debug("  vid = {:#06x}", libevdev_get_id_vendor(evdev));
    log_debug("  pid = {:#06x}", libevdev_get_id_product(evdev));

    auto device = ref_create<IoEvdevDevice>();
    device->fd    = std::exchange(fd,    -1);
    device->evdev = std::exchange(evdev, nullptr);
    ctx->evdev->devices.emplace_back(device.get());

    exec_fd_listen(ctx->exec, device->fd, FdEventBit::readable, [ctx, device = device.get()](int, Flags<FdEventBit>) {
        handle_evdev_event(ctx, device);
    });
}

static
void handle_monitor_events(IoContext* ctx)
{
    for (;;) {
        auto dev = udev_monitor_receive_device(ctx->evdev->monitor);
        if (!dev) break;
        defer { udev_device_unref(dev); };

        auto action = udev_device_get_action(dev);
        if ("add"sv == action) handle_device(ctx, dev);
    }
}

void io_evdev_init(IoContext* ctx)
{
    ctx->evdev = ref_create<IoEvdev>();

    auto enumerate = udev_enumerate_new(ctx->udev);
    defer { udev_enumerate_unref(enumerate); };

    udev_enumerate_add_match_subsystem(enumerate, "input");
    udev_enumerate_scan_devices(enumerate);

    auto devices = udev_enumerate_get_list_entry(enumerate);
    udev_list_entry* entry;
    udev_list_entry_foreach(entry, devices) {
        auto syspath = udev_list_entry_get_name(entry);
        auto dev = udev_device_new_from_syspath(ctx->udev, syspath);
        defer { udev_device_unref(dev); };

        handle_device(ctx, dev);
    }

    ctx->evdev->monitor = udev_monitor_new_from_netlink(ctx->udev, "udev");
    udev_monitor_filter_add_match_subsystem_devtype(ctx->evdev->monitor, "input", nullptr);
    udev_monitor_enable_receiving(ctx->evdev->monitor);
    auto fd = udev_monitor_get_fd(ctx->evdev->monitor);
    exec_fd_listen(ctx->exec, fd, FdEventBit::readable, [ctx](int, Flags<FdEventBit>) {
        handle_monitor_events(ctx);
    });
}

void io_evdev_deinit(IoContext* ctx)
{
    for (auto& device : ctx->evdev->devices) {
        device->destroy(ctx);
    }

    exec_fd_unlisten(ctx->exec, udev_monitor_get_fd(ctx->evdev->monitor));
    udev_monitor_unref(ctx->evdev->monitor);

    ctx->evdev.destroy();
}
