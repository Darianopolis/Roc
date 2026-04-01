#include "internal.hpp"

auto io_create(ExecContext* exec, Gpu* gpu) -> Ref<IoContext>
{
    auto io = ref_create<IoContext>();

    io->exec = exec;
    io->gpu = gpu;

    io_udev_init(    io.get());
    io_session_init( io.get());
    io_libinput_init(io.get());
    io_evdev_init(   io.get());
    io_drm_init(     io.get());
    io_wayland_init( io.get());

    return io;
}

static
void shutdown(IoContext* io)
{
    io_session_deinit(io);
    io_libinput_deinit(io);
    io_evdev_deinit(io);
    io_drm_deinit(io);
    io_wayland_deinit(io);
    io_udev_deinit(io);

    exec_stop(io->exec);
}

IoContext::~IoContext()
{
    debug_assert(!wayland);
    debug_assert(!drm);
    debug_assert(!evdev);
    debug_assert(!libinput);
    debug_assert(!session);
}

void io_set_event_handler(IoContext* io, std::move_only_function<IoEventHandler>&& handler)
{
    io->event_handler = std::move(handler);
}

static
Weak<IoContext> signal_context;

static
void signal_handler(int sig)
{
    if (sig == SIGINT) {
        // Immediately unregister SIGINT in case application is unresponsive
        std::signal(sig, SIG_DFL);
    }

    if (!signal_context) return;

    switch (sig) {
        break;case SIGTERM: io_request_shutdown(signal_context.get(), IoShutdownReason::terminate_received);
        break;case SIGINT:  io_request_shutdown(signal_context.get(), IoShutdownReason::interrupt_received);
    }
}

void io_run(IoContext* io)
{
    debug_assert(io->event_handler);

    signal_context = io;
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    if (io->wayland) {
        io_wayland_start(io);
    }

    if (io->drm) {
        io_drm_start(io);
    }

    exec_run(io->exec);

    signal_context = nullptr;
    signal(SIGINT, SIG_DFL);
    signal(SIGTERM, SIG_IGN);
}

void io_request_shutdown(IoContext* io, IoShutdownReason reason)
{
    exec_enqueue(io->exec, [io, reason] {
        io_post_event(io, ptr_to(IoEvent {
            .type = IoEventType::shutdown_requested,
            .shutdown {
                .reason = reason,
            }
        }));
    });
}

void io_stop(IoContext* io)
{
    if (io->stop_requested) return;
    io->stop_requested = true;

    exec_enqueue(io->exec, [io] {
        shutdown(io);
    });
}

void io_post_event(IoContext* io, IoEvent* event)
{
    io->event_handler(event);
}
