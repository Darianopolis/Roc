#include "backend.hpp"

wroc_device* open_device(wroc_direct_backend* backend, const char* path)
{
    int fd = -1;
    auto dev_id = libseat_open_device(backend->seat, path, &fd);
    if (fd < 0) {
        log_error("Failed to open device");
        return nullptr;
    }

    auto device = core::create<wroc_device>();
    device->dev_id = dev_id;
    device->fd = fd;

    backend->devices.emplace_back(device);

    return device.get();
}

wroc_device* wroc_open_restricted(wroc_direct_backend* backend, const char* name)
{
    return open_device(backend, name);
}

void close_device(wroc_direct_backend* backend, wroc_device* device)
{
    libseat_close_device(backend->seat, device->dev_id);
    close(device->fd);
    std::erase_if(backend->devices, [&](const auto& d) { return d.get() == device; });
}

// -----------------------------------------------------------------------------

static
void seat_enable(libseat* seat, void* data)
{
    log_debug("SEAT ENABLE");
}

static
void seat_disable(libseat* seat, void* data)
{
    log_debug("SEAT DISABLE");
}

static constexpr libseat_seat_listener wroc_seat_listener {
    .enable_seat = seat_enable,
    .disable_seat = seat_disable,
};

static
int handle_libseat_readable(wroc_direct_backend* backend, int fd, core::Flags<core::FdEventBit> events)
{
    log_debug("SEAT DISPATCH");
    libseat_dispatch(backend->seat, 0);
    return 0;
}

// -----------------------------------------------------------------------------

static
int open_restricted(const char* path, int flags, void* data)
{
    log_debug("OPEN RESTRICTED");
    log_debug("  path: {}", path ?: "nullptr");
    auto* backend = static_cast<wroc_direct_backend*>(data);

    auto device = open_device(backend, path);
    if (!device) {
        log_error("Failed to open");
        return -1;
    }

    log_info("  device.id: {}", device->dev_id);
    log_info("  device.fd: {}", device->fd);

    return device->fd;
}

static
void close_restricted(int fd, void* data)
{
    log_debug("CLOSE RESTRICTED: {}", fd);
    auto* backend = static_cast<wroc_direct_backend*>(data);

    auto iter = std::ranges::find(backend->devices, fd, [&](const auto& d) { return d->fd; });
    if (iter != backend->devices.end()) {
        log_debug("Closing device: (id = {})", iter->get()->dev_id);
        close_device(backend, iter->get());
    } else {
        log_error("Could not find open device for fd: {}", fd);
    }
}

static constexpr libinput_interface wroc_libinput_interface {
    .open_restricted = open_restricted,
    .close_restricted = close_restricted,
};

static
int handle_libinput_readable(wroc_direct_backend* backend, int fd, core::Flags<core::FdEventBit> events)
{
    core::check<libinput_dispatch>(backend->libinput);

    libinput_event* event;
    while ((event = libinput_get_event(backend->libinput))) {
        wroc_backend_handle_libinput_event(backend, event);
        libinput_event_destroy(event);
    }

    return 0;
}

// -----------------------------------------------------------------------------

static
void log_libinput(libinput* libinput, libinput_log_priority priority, const char* fmt, va_list args) {
    core::LogLevel level;
    switch (priority) {
        break;case LIBINPUT_LOG_PRIORITY_ERROR: level = core::LogLevel::error;
        break;case LIBINPUT_LOG_PRIORITY_INFO: level = core::LogLevel::info;
        break;default: level = core::LogLevel::debug;
    }

    static char wlr_fmt[4096] = {};
    vsnprintf(wlr_fmt, sizeof(wlr_fmt) - 1, fmt, args);
    core::log(level, std::format("[libinput] {}", wlr_fmt));
}

// -----------------------------------------------------------------------------

void wroc_backend_init_session(wroc_direct_backend* backend)
{
    // udev

    log_debug("Init libinput backend for {}", (void*)backend);

    backend->udev = udev_new();
    if (!backend->udev) {
        log_error("Input backend init failed: Failed to create udev context");
        return;
    }

    // libseat

    wroc_setenv("XDG_SESSION_TYPE", "wayland", wroc_setenv_option::system_wide);

    backend->seat = core::check<libseat_open_seat>(&wroc_seat_listener, nullptr).value;
    if (!backend->seat) {
        log_error("Failed to open seat");
        core::debugkill();
    }

    backend->seat_name = libseat_seat_name(backend->seat);
    log_info("Seat name: {}", backend->seat_name);

    int seat_fd = core::check<libseat_get_fd>(backend->seat).value;
    core_assert(seat_fd >= 0);

    backend->libseat_fd = core::fd::reference(seat_fd);
    core::fd::add_listener(backend->libseat_fd.get(), server->event_loop.get(), core::FdEventBit::readable,
        [backend](int fd, core::Flags<core::FdEventBit> events) {
            handle_libseat_readable(backend, fd, events);
        });

    // libinput

    backend->libinput = libinput_udev_create_context(&wroc_libinput_interface, backend, backend->udev);
    if (!backend->libinput) {
        log_error("Failed to create libinput context");
        core::debugkill();
    }

    libinput_log_set_handler(backend->libinput, log_libinput);
    libinput_log_set_priority(backend->libinput, LIBINPUT_LOG_PRIORITY_DEBUG);

    if (core::check<libinput_udev_assign_seat>(backend->libinput, backend->seat_name).err()) {
        log_error("Libinput failed to acquire seat");
        core::debugkill();
    }

    int libinput_fd = libinput_get_fd(backend->libinput);
    log_debug("Libinput fd = {}", libinput_fd);

    handle_libinput_readable(backend, backend->libinput_fd.get(), core::FdEventBit::readable);
    if (backend->input_devices.empty()) {
        log_error("Libinput initialization failed, no keyboard or mouse detected");
        core::debugkill();
    }

    backend->libinput_fd = core::fd::reference(libinput_fd);
    core::fd::add_listener(backend->libinput_fd.get(), server->event_loop.get(), core::FdEventBit::readable,
        [backend](int fd, core::Flags<core::FdEventBit> events) {
            handle_libinput_readable(backend, fd, events);
        });

    log_debug("Libinput fd event source added");
}

void wroc_backend_close_session(wroc_direct_backend* backend)
{
    backend->input_devices.clear();

    backend->libinput_fd = nullptr;
    libinput_unref(backend->libinput);

    backend->libseat_fd = nullptr;
    libseat_close_seat(backend->seat);

    udev_unref(backend->udev);
}
