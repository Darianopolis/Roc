#include "backend.hpp"

// -----------------------------------------------------------------------------

static
void wroc_listen_xdg_wm_base_ping(void*, struct xdg_wm_base* xdg_wm_base, u32 serial)
{
    log_trace("xdg_wm_base::ping(serial = {})", serial);

    xdg_wm_base_pong(xdg_wm_base, serial);
}

const xdg_wm_base_listener wroc_xdg_wm_base_listener = {
    .ping = wroc_listen_xdg_wm_base_ping,
};

// -----------------------------------------------------------------------------

static
void wroc_listen_registry_global(void* data, wl_registry*, u32 name, const char* interface, u32 version)
{
    auto* backend = static_cast<wroc_wayland_backend*>(data);

    auto match_interface = [&](const wl_interface* wl_interface, auto member) -> bool {
        if (strcmp(interface, wl_interface->name) != 0) {
            return false;
        }

        u32 bound_version = std::min(version, u32(wl_interface->version));

        backend->*member = static_cast<std::remove_cvref_t<decltype(backend->*member)>>(
            wl_registry_bind(backend->wl_registry, name, wl_interface, bound_version));

        log_info("wl_global[{:2} : {:41}], version = {} (bound: {})", name, interface, version, bound_version);

        return true;
    };

#define MATCH_BEGIN                if (false) {}
#define MATCH_INTERFACE(Interface) else if (match_interface(&Interface##_interface, &wroc_wayland_backend::Interface))
#define MATCH_END                  else log_trace("wl_global[{:2} : {:41}], version = {}", name, interface, version);

    MATCH_BEGIN
    MATCH_INTERFACE(wl_compositor) {}
    MATCH_INTERFACE(xdg_wm_base) {
        xdg_wm_base_add_listener(backend->xdg_wm_base, &wroc_xdg_wm_base_listener, backend);
    }
    MATCH_INTERFACE(zxdg_decoration_manager_v1) {}
    MATCH_INTERFACE(wl_seat) {
        wl_seat_add_listener(backend->wl_seat, &wroc_wl_seat_listener, backend);
    }
    MATCH_INTERFACE(zwp_relative_pointer_manager_v1){}
    MATCH_INTERFACE(zwp_pointer_constraints_v1) {}
    MATCH_INTERFACE(zwp_linux_dmabuf_v1) {
        auto feedback = zwp_linux_dmabuf_v1_get_default_feedback(backend->zwp_linux_dmabuf_v1);
        zwp_linux_dmabuf_feedback_v1_add_listener(feedback, &wroc_zwp_linux_dmabuf_feedback_v1_listener, backend);
    }
    MATCH_INTERFACE(wp_linux_drm_syncobj_manager_v1) {}
    MATCH_END

#undef MATCH_BEGIN
#undef MATCH_INTERFACE
#undef MATCH_END
}

static
void wroc_listen_registry_global_remove(void*, wl_registry*, u32 name)
{
    log_debug("wl_registry::global_remove(name = {:2})", name);
}

const wl_registry_listener wroc_wl_registry_listener {
    .global        = wroc_listen_registry_global,
    .global_remove = wroc_listen_registry_global_remove,
};

static
int wroc_listen_backend_display_read(wroc_wayland_backend* backend, int fd, flags<core_fd_event_bit> events)
{
    backend->current_dispatch_time = std::chrono::steady_clock::now();

    timespec timeout = {};
    if (wl_display_dispatch_timeout(backend->wl_display, &timeout) == -1) {
        log_error("wl_display_dispatch_timeout failed: {}", strerror(errno));
        backend->wl_event_source_fd = nullptr;
        return false;
    }
    wl_display_flush(backend->wl_display);

    return 1;
}

void wroc_wayland_backend::init()
{
    if (getenv("WROC_WAYLAND_DEBUG_BACKEND")) {
        wroc_setenv("WAYLAND_DEBUG", "1");
    } else {
        wroc_setenv("WAYLAND_DEBUG", nullptr);
    }
    wl_display = wl_display_connect(nullptr);
    wroc_setenv("WAYLAND_DEBUG", nullptr);
    wl_registry = wl_display_get_registry(wl_display);

    wl_registry_add_listener(wl_registry, &wroc_wl_registry_listener, this);

    // First roundtrip binds interfaces
    wl_display_roundtrip(wl_display);

    // Second roundtrip ensures that all events expected in response to binding are received
    wl_display_roundtrip(wl_display);

    wl_event_source_fd = core_fd_reference(wl_display_get_fd(wl_display));
    core_fd_add_listener(wl_event_source_fd.get(), server->event_loop.get(), core_fd_event_bit::readable,
        [backend = weak(this)](int fd, flags<core_fd_event_bit> events) {
            if (backend) {
                wroc_listen_backend_display_read(backend.get(), fd, events);
            }
        });

    wl_display_flush(wl_display);
}

void wroc_wayland_backend::start()
{
    create_output();

    wl_display_flush(wl_display);
}

wroc_wayland_backend::~wroc_wayland_backend()
{
    if (keyboard) keyboard = nullptr;
    if (pointer)  pointer = nullptr;

    wl_event_source_fd = nullptr;

    outputs.clear();

    zwp_linux_dmabuf_v1_destroy(zwp_linux_dmabuf_v1);
    syncobj_cache.entries.clear();
    buffer_cache.entries.clear();
    wp_linux_drm_syncobj_manager_v1_destroy(wp_linux_drm_syncobj_manager_v1);

    zwp_relative_pointer_manager_v1_destroy(zwp_relative_pointer_manager_v1);
    zwp_pointer_constraints_v1_destroy(zwp_pointer_constraints_v1);

    if (zxdg_decoration_manager_v1) zxdg_decoration_manager_v1_destroy(zxdg_decoration_manager_v1);
    wl_compositor_destroy(wl_compositor);
    xdg_wm_base_destroy(xdg_wm_base);
    wl_seat_destroy(wl_seat);

    wl_registry_destroy(wl_registry);

    wl_display_disconnect(wl_display);
}
