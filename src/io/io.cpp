#include "internal.hpp"

#include <core/log.hpp>

#define IO_EVDEV_ENABLED 0

auto io_create(ExecContext* exec, Gpu* gpu) -> Ref<IoContext>
{
    auto io = ref_create<IoContext>();

    io->exec = exec;
    io->gpu = gpu;

    io_udev_init(    io.get());
    io_session_init( io.get());
    io_libinput_init(io.get());
#if IO_EVDEV_ENABLED
    io_evdev_init(   io.get());
#endif
    io_drm_init(     io.get());
    io_wayland_init( io.get());

    return io;
}

static
void post_shutdown_request(IoContext* io, IoShutdownReason reason)
{
    io_post_event(io, ptr_to(IoEvent {
        .shutdown {
            .type = IoEventType::shutdown_requested,
            .reason = reason,
        }
    }));
}

static
void shutdown(IoContext* io)
{
    io_wayland_deinit(io);
    io_drm_deinit(io);
#if IO_EVDEV_ENABLED
    io_evdev_deinit(io);
#endif
    io_libinput_deinit(io);
    io_session_deinit(io);
    io_udev_deinit(io);

    io->signals.shutdown();
}

IoContext::~IoContext()
{
    fd_unlisten(exec, signal_fd.get());

    debug_assert(!wayland);
    debug_assert(!drm);
    debug_assert(!evdev);
    debug_assert(!libinput);
    debug_assert(!session);
}

auto io_get_signals(IoContext* io) -> IoSignals&
{
    return io->signals;
}

static
void handle_signal(IoContext* io)
{
    signalfd_siginfo info = {};
    unix_check<read>(io->signal_fd.get(), &info, sizeof(info));

    IoShutdownReason reason;
    switch (info.ssi_signo) {
        break;case SIGINT:  reason = IoShutdownReason::interrupt_received;
        break;case SIGTERM: reason = IoShutdownReason::terminate_received;
        break;default:      debug_unreachable();
    }

    post_shutdown_request(io, reason);
}

void io_start(IoContext* io)
{
    if (io->wayland) {
        io_wayland_start(io);
    }

    if (io->drm) {
        io_drm_start(io);
    }

    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    sigprocmask(SIG_BLOCK, &mask, nullptr);
    io->signal_fd = Fd(unix_check<signalfd>(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC).value);
    fd_listen(io->exec, io->signal_fd.get(), FdEventBit::readable, [io](fd_t, Flags<FdEventBit>){
        handle_signal(io);
    });
}

void io_request_shutdown(IoContext* io, IoShutdownReason reason)
{
    exec_enqueue(io->exec, [io = Weak(io), reason] {
        if (!io) return;
        post_shutdown_request(io.get(), reason);
    });
}

void io_stop(IoContext* io)
{
    if (io->stop_requested) return;
    io->stop_requested = true;

    exec_enqueue(io->exec, [io = Weak(io)] {
        if (!io) return;
        shutdown(io.get());
    });
}

void io_post_event(IoContext* io, IoEvent* event)
{
    io->signals.event(event);
}
