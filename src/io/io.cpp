#include "internal.hpp"

auto io_create(exec_context* exec, gpu_context* gpu) -> ref<io_context>
{
    auto ctx = core_create<io_context>();

    ctx->exec = exec;
    ctx->gpu = gpu;

    io_session_init( ctx.get());
    io_libinput_init(ctx.get());
    io_evdev_init(   ctx.get());
    io_drm_init(     ctx.get());
    io_wayland_init( ctx.get());

    return ctx;
}

static
void shutdown(io_context* ctx)
{
    io_session_deinit(ctx);
    io_libinput_deinit(ctx);
    io_evdev_deinit(ctx);
    io_drm_deinit(ctx);
    io_wayland_deinit(ctx);

    exec_stop(ctx->exec);
}

io_context::~io_context()
{
    core_assert(!wayland);
    core_assert(!drm);
    core_assert(!evdev);
    core_assert(!libinput);
    core_assert(!session);
}

void io_set_event_handler(io_context* ctx, std::move_only_function<io_event_handler>&& handler)
{
    ctx->event_handler = std::move(handler);
}

static
weak<io_context> signal_context;

static
void signal_handler(int sig)
{
    if (sig == SIGINT) {
        // Immediately unregister SIGINT in case application is unresponsive
        std::signal(sig, SIG_DFL);
    }

    if (!signal_context) return;

    switch (sig) {
        break;case SIGTERM: io_request_shutdown(signal_context.get(), io_shutdown_reason::terminate_received);
        break;case SIGINT:  io_request_shutdown(signal_context.get(), io_shutdown_reason::interrupt_received);
    }
}

void io_run(io_context* ctx)
{
    core_assert(ctx->event_handler);

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

void io_request_shutdown(io_context* ctx, io_shutdown_reason reason)
{
    exec_enqueue(ctx->exec, [ctx, reason] {
        io_post_event(ctx, ptr_to(io_event {
            .type = io_event_type::shutdown_requested,
            .shutdown {
                .reason = reason,
            }
        }));
    });
}

void io_stop(io_context* ctx)
{
    if (ctx->stop_requested) return;
    ctx->stop_requested = true;

    exec_enqueue(ctx->exec, [ctx] {
        shutdown(ctx);
    });
}

void io_post_event(io_context* ctx, io_event* event)
{
    ctx->event_handler(event);
}
