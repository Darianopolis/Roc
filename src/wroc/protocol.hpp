#pragma once

#include "core/log.hpp"
#include "core/object.hpp"

#include "wroc.hpp"

// -----------------------------------------------------------------------------

template<typename T>
T* wroc_try_get_userdata(wl_resource* resource)
{
    if (!resource) return nullptr;
    return dynamic_cast<T*>(static_cast<wroc_object*>(wl_resource_get_user_data(resource)));
}

template<typename T>
T* wroc_get_userdata(wl_resource* resource)
{
    if (!resource) return nullptr;
    auto* base = static_cast<wroc_object*>(wl_resource_get_user_data(resource));
    if (!base) return nullptr;
    auto* cast = dynamic_cast<T*>(base);
    if (!cast) {
        log_error("Fatal error casting wl_resource userdata: expected {} got {}", typeid(T).name(), typeid(*base).name());
        core_debugbreak();
    }
    return cast;
}

#define WROC_NOISY_WL_RESOURCE 0

inline
wl_resource* wroc_resource_create(wl_client* client, const wl_interface* interface, int version, u32 id)
{
    auto resource = wl_resource_create(client, interface, version, id);
#if WROC_NOISY_WL_RESOURCE
    static i64 count = 0;
    log_trace("wl_resource ++ {}", ++count);

    auto* destroy_listener = new wl_listener {};
    destroy_listener->notify = [](wl_listener* listener, void*) {
        log_trace("wl_resource -- {}", --count);
        wl_list_remove(&listener->link);
        delete listener;
    };
    wl_resource_add_destroy_listener(resource, destroy_listener);
#endif
    return resource;
}

inline
void wroc_resource_simple_unref(wl_resource* resource)
{
    core_remove_ref(static_cast<wroc_object*>(wl_resource_get_user_data(resource)));
}

inline
void wroc_resource_set_implementation_refcounted(wl_resource* resource, const void* implementation, wroc_object* base)
{
    wl_resource_set_implementation(resource, implementation, base, wroc_resource_simple_unref);
}

inline
void wroc_resource_set_implementation(wl_resource* resource, const void* implementation, wroc_object* base)
{
    wl_resource_set_implementation(resource, implementation, base, nullptr);
}

inline
void wroc_simple_resource_destroy_callback(wl_client* client, wl_resource* resource)
{
    if (auto* addon = wroc_try_get_userdata<wroc_surface_addon>(resource)) {
        wroc_surface_addon_detach(addon);
    }
    wl_resource_destroy(resource);
}

// -----------------------------------------------------------------------------

#define WROC_INTERFACE(Name, ...) \
    extern const struct Name##_interface wroc_##Name##_impl \
    __VA_OPT__(; \
        extern const u32 wroc_##Name##_version; \
        void wroc_##Name##_bind_global(wl_client*, void*, u32 version, u32 id) \
    )

WROC_INTERFACE(wl_compositor, true);
WROC_INTERFACE(wl_region);
WROC_INTERFACE(wl_surface);

WROC_INTERFACE(wl_subcompositor, true);
WROC_INTERFACE(wl_subsurface);

WROC_INTERFACE(wl_output, true);

WROC_INTERFACE(xdg_wm_base, true);
WROC_INTERFACE(xdg_surface);
WROC_INTERFACE(xdg_toplevel);
WROC_INTERFACE(xdg_positioner);
WROC_INTERFACE(xdg_popup);

WROC_INTERFACE(wl_buffer);

WROC_INTERFACE(wl_shm, true);
WROC_INTERFACE(wl_shm_pool);

WROC_INTERFACE(wl_seat, true);
WROC_INTERFACE(wl_keyboard);
WROC_INTERFACE(wl_pointer);

WROC_INTERFACE(wl_data_device_manager, true);
WROC_INTERFACE(wl_data_offer);
WROC_INTERFACE(wl_data_source);
WROC_INTERFACE(wl_data_device);

WROC_INTERFACE(zwp_linux_dmabuf_v1, true);
WROC_INTERFACE(zwp_linux_buffer_params_v1);
WROC_INTERFACE(zwp_linux_dmabuf_feedback_v1);

WROC_INTERFACE(zwp_pointer_gestures_v1, true);
WROC_INTERFACE(zwp_pointer_gesture_swipe_v1);
WROC_INTERFACE(zwp_pointer_gesture_pinch_v1);
WROC_INTERFACE(zwp_pointer_gesture_hold_v1);

WROC_INTERFACE(wp_viewporter, true);
WROC_INTERFACE(wp_viewport);

WROC_INTERFACE(zwp_relative_pointer_manager_v1, true);
WROC_INTERFACE(zwp_relative_pointer_v1);

WROC_INTERFACE(zwp_pointer_constraints_v1, true);
WROC_INTERFACE(zwp_locked_pointer_v1);
WROC_INTERFACE(zwp_confined_pointer_v1);

WROC_INTERFACE(zxdg_decoration_manager_v1, true);
WROC_INTERFACE(zxdg_toplevel_decoration_v1);

WROC_INTERFACE(org_kde_kwin_server_decoration_manager, true);
WROC_INTERFACE(org_kde_kwin_server_decoration);

WROC_INTERFACE(wp_cursor_shape_manager_v1, true);
WROC_INTERFACE(wp_cursor_shape_device_v1);

WROC_INTERFACE(wp_linux_drm_syncobj_manager_v1, true);
WROC_INTERFACE(wp_linux_drm_syncobj_timeline_v1);
WROC_INTERFACE(wp_linux_drm_syncobj_surface_v1);
