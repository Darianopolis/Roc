#include "libinput.hpp"

#include "../session/session.hpp"

#include <core/log.hpp>

static constexpr libinput_interface io_libinput_interface {

    .open_restricted = [](const char* path, int flags, void* data) {
        auto* io = static_cast<IoContext*>(data);
        return io_session_open_device(io->session.get(), path);
    },
    .close_restricted = [](fd_t fd, void* data) {
        auto* io = static_cast<IoContext*>(data);
        io_session_close_device(io->session.get(), fd);
    }
};

static
auto handle_libinput_readable(IoContext* io) -> int
{
    unix_check<libinput_dispatch>(io->libinput->libinput);

    libinput_event* event;
    while ((event = libinput_get_event(io->libinput->libinput))) {
        io_libinput_handle_event(io, event);
        libinput_event_destroy(event);
    }

    return 0;
}

void io_libinput_init(IoContext* io)
{
    if (!io->session) {
        log_warn("Can't start libinput, session backend not started");
        return;
    }

    io->libinput = ref_create<IoLibinput>();
    io->libinput->libinput = libinput_udev_create_context(&io_libinput_interface, io, io->udev);
    debug_assert(io->libinput->libinput);

    debug_assert(unix_check<libinput_udev_assign_seat>(io->libinput->libinput, io_session_get_seat_name(io->session.get())).ok());

    fd_t fd = libinput_get_fd(io->libinput->libinput);
    fd_listen(io->exec, fd, FdEventBit::readable, [io](fd_t fd, Flags<FdEventBit>) {
        handle_libinput_readable(io);
    });
}

void io_libinput_deinit(IoContext* io)
{
    if (!io->libinput) return;

    io->libinput->input_devices.destroy_all();

    fd_unlisten(io->exec, libinput_get_fd(io->libinput->libinput));
    libinput_unref(io->libinput->libinput);

    io->libinput.destroy();
}
