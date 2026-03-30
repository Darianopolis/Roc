#include "wayland.hpp"

// -----------------------------------------------------------------------------

static
void registry_global(void* data, wl_registry*, u32 name, const char* interface, u32 version)
{
    auto* ctx = static_cast<IoContext*>(data);
    auto* wl = ctx->wayland.get();

    auto match_interface = [&](const wl_interface* wl_interface, auto member) -> bool {
        if (strcmp(interface, wl_interface->name) != 0) {
            return false;
        }

        u32 bound_version = std::min(version, u32(wl_interface->version));

        debug_assert(!(wl->*member), "Interface <{}> already bound", interface);
        wl->*member = static_cast<std::remove_cvref_t<decltype(wl->*member)>>(
            wl_registry_bind(wl->wl_registry, name, wl_interface, bound_version));

        log_info("wl_global[{:2} : {:41}], version = {} (bound: {})", name, interface, version, bound_version);

        return true;
    };

#define BIND_BEGIN                if (false) {}
#define BIND_INTERFACE(Interface) else if (match_interface(&Interface##_interface, &IoWayland::Interface))
#define BIND_END                  else log_trace("wl_global[{:2} : {:41}], version = {}", name, interface, version);

    BIND_BEGIN
    BIND_INTERFACE(wl_compositor) {}
    BIND_INTERFACE(xdg_wm_base) {
        xdg_wm_base_add_listener(wl->xdg_wm_base, &io_xdg_wm_base_listener, ctx);
    }
    BIND_INTERFACE(zxdg_decoration_manager_v1) {}
    BIND_INTERFACE(wl_seat) {
        wl_seat_add_listener(wl->wl_seat, &io_wl_seat_listener, ctx);
    }
    BIND_INTERFACE(zwp_relative_pointer_manager_v1){}
    BIND_INTERFACE(zwp_pointer_constraints_v1) {}
    BIND_INTERFACE(zwp_linux_dmabuf_v1) {
        auto feedback = zwp_linux_dmabuf_v1_get_default_feedback(wl->zwp_linux_dmabuf_v1);
        zwp_linux_dmabuf_feedback_v1_add_listener(feedback, &io_zwp_linux_dmabuf_feedback_v1_listener, ctx);
    }
    BIND_INTERFACE(wp_linux_drm_syncobj_manager_v1) {}
    BIND_END

#undef BIND_BEGIN
#undef BIND_INTERFACE
#undef BIND_END
}

IO_WL_LISTENER(wl_registry) = {
    .global = registry_global,
    IO_WL_STUB(wl_registry, global_remove),
};

// -----------------------------------------------------------------------------

IO_WL_LISTENER(xdg_wm_base) = {
    .ping = [](void*, xdg_wm_base* base, u32 serial) {
        xdg_wm_base_pong(base, serial);
    }
};

// -----------------------------------------------------------------------------

static
void display_read(IoContext* ctx, Flags<FdEventBit> events)
{
    ctx->wayland->current_dispatch_time = std::chrono::steady_clock::now();

    timespec timeout = {};
    if (unix_check<wl_display_dispatch_timeout>(ctx->wayland->wl_display, &timeout).err()) {
        debug_kill();
    }

    wl_display_flush(ctx->wayland->wl_display);
}

void io_wayland_init(IoContext* ctx)
{
    if (ctx->session) {
        log_info("Session enabled, skipping io::Wayland backend");
        return;
    }

    ctx->wayland = ref_create<IoWayland>();
    auto* wl = ctx->wayland.get();

    wl->wl_display = wl_display_connect(nullptr);
}

void io_wayland_start(IoContext* ctx)
{
    auto* wl = ctx->wayland.get();

    wl->wl_registry = wl_display_get_registry(wl->wl_display);
    wl_registry_add_listener(wl->wl_registry, &io_wl_registry_listener, ctx);

    // First roundtrip binds interfaces
    wl_display_roundtrip(wl->wl_display);

    // Second roundtrip ensure that all events expected in response to binding are received
    wl_display_roundtrip(wl->wl_display);

    exec_fd_listen(ctx->exec, wl_display_get_fd(wl->wl_display), FdEventBit::readable,
        [ctx = Weak(ctx)](int, Flags<FdEventBit> events) {
            if (ctx) display_read(ctx.get(), events);
        });

    io_add_output(ctx);
}

void io_wayland_deinit(IoContext* ctx)
{
    exec_fd_unlisten(ctx->exec, wl_display_get_fd(ctx->wayland->wl_display));

    ctx->wayland.destroy();
}

IoWayland::~IoWayland()
{
    if (keyboard) keyboard = nullptr;
    if (pointer)  pointer = nullptr;

    outputs.clear();

    IO_WL_DESTROY(zwp_linux_dmabuf_v1);
    syncobj_cache.entries.clear();
    buffer_cache.entries.clear();
    IO_WL_DESTROY(wp_linux_drm_syncobj_manager_v1);

    IO_WL_DESTROY(zwp_relative_pointer_manager_v1);
    IO_WL_DESTROY(zwp_pointer_constraints_v1);

    IO_WL_DESTROY(zxdg_decoration_manager_v1);
    IO_WL_DESTROY(wl_compositor);
    IO_WL_DESTROY(xdg_wm_base);
    IO_WL_DESTROY(wl_seat);

    IO_WL_DESTROY(wl_registry);

    wl_display_disconnect(wl_display);
}
