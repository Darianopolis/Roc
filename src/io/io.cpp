#include "internal.hpp"

auto io_create(core::EventLoop* event_loop, gpu_context* gpu) -> core::Ref<io_context>
{
    auto ctx = core::create<io_context>();

    ctx->event_loop = event_loop;
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
    ctx->wayland.destroy();
    ctx->drm.destroy();
    ctx->evdev.destroy();
    ctx->libinput.destroy();
    ctx->session.destroy();

    core::event_loop::stop(ctx->event_loop);
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
core::Weak<io_context> signal_context;

static
void signal_handler(int sig)
{
    if (sig == SIGINT) {
        // Immediately unregister SIGINT in case of unresponsive event loop
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

    core::event_loop::run(ctx->event_loop);

    signal_context = nullptr;
    signal(SIGINT, SIG_DFL);
    signal(SIGTERM, SIG_IGN);
}

void io_request_shutdown(io_context* ctx, io_shutdown_reason reason)
{
    core::event_loop::enqueue(ctx->event_loop, [ctx, reason] {
        io_post_event(ctx, core::ptr_to(io_event {
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

    core::event_loop::enqueue(ctx->event_loop, [ctx] {
        shutdown(ctx);
    });
}

void io_post_event(io_context* ctx, io_event* event)
{
    ctx->event_handler(event);
}
