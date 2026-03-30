#include "../internal.hpp"

#include "../session/session.hpp"

struct IoLibinput
{
    struct libinput* libinput;
};

static constexpr libinput_interface io_libinput_interface {

    .open_restricted = [](const char* path, int flags, void* data) {
        auto* ctx = static_cast<IoContext*>(data);
        return io_session_open_device(ctx->session.get(), path);
    },
    .close_restricted = [](int fd, void* data) {
        auto* ctx = static_cast<IoContext*>(data);
        io_session_close_device(ctx->session.get(), fd);
    }
};

static
int handle_libinput_readable(IoContext* ctx)
{
    unix_check<libinput_dispatch>(ctx->libinput->libinput);

    libinput_event* event;
    while ((event = libinput_get_event(ctx->libinput->libinput))) {
        log_trace("libinput event");
        libinput_event_destroy(event);
    }

    return 0;
}

void io_libinput_init(IoContext* ctx)
{
    if (!ctx->session) {
        log_warn("Can't start libinput, session backend not started");
        return;
    }

    ctx->libinput = ref_create<IoLibinput>();
    ctx->libinput->libinput = libinput_udev_create_context(&io_libinput_interface, ctx, ctx->udev);
    debug_assert(ctx->libinput->libinput);

    debug_assert(unix_check<libinput_udev_assign_seat>(ctx->libinput->libinput, io_session_get_seat_name(ctx->session.get())).ok());

    int fd = libinput_get_fd(ctx->libinput->libinput);
    exec_fd_listen(ctx->exec, fd, FdEventBit::readable, [ctx](int fd, Flags<FdEventBit>) {
        handle_libinput_readable(ctx);
    });
}

void io_libinput_deinit(IoContext* ctx)
{
    if (ctx->libinput) {
        exec_fd_unlisten(ctx->exec, libinput_get_fd(ctx->libinput->libinput));
        libinput_unref(ctx->libinput->libinput);
    }

    ctx->libinput.destroy();
}
