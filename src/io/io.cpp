#include "internal.hpp"

auto io_create(ExecContext* exec, Gpu* gpu) -> Ref<IoContext>
{
    auto ctx = ref_create<IoContext>();

    ctx->exec = exec;
    ctx->gpu = gpu;

    io_udev_init(    ctx.get());
    io_session_init( ctx.get());
    io_libinput_init(ctx.get());
    io_evdev_init(   ctx.get());
    io_drm_init(     ctx.get());
    io_wayland_init( ctx.get());

    return ctx;
}

static
void shutdown(IoContext* ctx)
{
    io_session_deinit(ctx);
    io_libinput_deinit(ctx);
    io_evdev_deinit(ctx);
    io_drm_deinit(ctx);
    io_wayland_deinit(ctx);
    io_udev_deinit(ctx);

    exec_stop(ctx->exec);
}

IoContext::~IoContext()
{
    debug_assert(!wayland);
    debug_assert(!drm);
    debug_assert(!evdev);
    debug_assert(!libinput);
    debug_assert(!session);
}

void io_set_event_handler(IoContext* ctx, std::move_only_function<IoEventHandler>&& handler)
{
    ctx->event_handler = std::move(handler);
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

void io_run(IoContext* ctx)
{
    debug_assert(ctx->event_handler);

    signal_context = ctx;
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    if (ctx->wayland) {
        io_wayland_start(ctx);
    }

    exec_run(ctx->exec);

    signal_context = nullptr;
    signal(SIGINT, SIG_DFL);
    signal(SIGTERM, SIG_IGN);
}

void io_request_shutdown(IoContext* ctx, IoShutdownReason reason)
{
    exec_enqueue(ctx->exec, [ctx, reason] {
        io_post_event(ctx, ptr_to(IoEvent {
            .type = IoEventType::shutdown_requested,
            .shutdown {
                .reason = reason,
            }
        }));
    });
}

void io_stop(IoContext* ctx)
{
    if (ctx->stop_requested) return;
    ctx->stop_requested = true;

    exec_enqueue(ctx->exec, [ctx] {
        shutdown(ctx);
    });
}

void io_post_event(IoContext* ctx, IoEvent* event)
{
    ctx->event_handler(event);
}
