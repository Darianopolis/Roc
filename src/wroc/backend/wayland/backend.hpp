#pragma once

#include "wroc/wroc.hpp"

// -----------------------------------------------------------------------------

#include <wayland-client-core.h>

#include <wayland/client/xdg-shell.h>
#include <wayland/client/xdg-decoration-unstable-v1.h>
#include <wayland/client/relative-pointer-unstable-v1.h>
#include <wayland/client/pointer-constraints-unstable-v1.h>
#include <wayland/client/linux-dmabuf-v1.h>
#include <wayland/client/linux-drm-syncobj-v1.h>

// -----------------------------------------------------------------------------

struct wroc_wayland_backend;

template<typename K, typename V>
struct wroc_wl_proxy_cache
{
    using Vptr = std::unique_ptr<V, void(*)(V*)>;
    struct entry { core::Weak<K> key; Vptr value; };

    void(*destroy)(V*);
    std::vector<entry> entries;

    V* find(K* needle)
    {
        V* found = nullptr;
        std::erase_if(entries, [&](const auto& entry) {
            if (!entry.key) return true;
            if (entry.key.get() == needle) found = entry.value.get();
            return false;
        });
        return found;
    }

    V* insert(K* key, V* value)
    {
        return entries.emplace_back(key, Vptr(value, destroy)).value.get();
    }
};

struct wroc_wayland_commit_feedback
{
    wroc_output_commit_id commit_id;
    std::chrono::steady_clock::time_point commit_time;
};

struct wroc_wayland_output : wroc_output
{
    wp_linux_drm_syncobj_surface_v1* syncobj_surface;

    struct wl_surface* wl_surface = {};
    struct xdg_surface* xdg_surface = {};
    xdg_toplevel* toplevel = {};
    zxdg_toplevel_decoration_v1* decoration = {};
    zwp_locked_pointer_v1* locked_pointer = {};
    bool locked = false;

    wl_callback* frame_callback = {};
    std::vector<wroc_wayland_commit_feedback> pending_feedback = {};

    ~wroc_wayland_output();

    virtual wroc_output_commit_id commit(gpu::Image*, gpu::Syncpoint acquire, gpu::Syncpoint release, core::Flags<wroc_output_commit_flag>) final override;
};

struct wroc_wayland_keyboard : wroc_keyboard
{
    struct wl_keyboard* wl_keyboard = {};

    ~wroc_wayland_keyboard();
};

struct wroc_wayland_pointer : wroc_pointer
{
    struct wl_pointer* wl_pointer = {};
    u32 last_serial = {};

    struct zwp_relative_pointer_v1* relative_pointer = {};
    core::Weak<wroc_wayland_output> current_output = {};

    ~wroc_wayland_pointer();
};

struct wroc_wayland_backend : wroc_backend
{
    struct wl_display* wl_display = {};
    struct wl_registry* wl_registry = {};
    struct wl_compositor* wl_compositor;
    struct xdg_wm_base* xdg_wm_base = {};
    struct zxdg_decoration_manager_v1* zxdg_decoration_manager_v1 = {};
    struct zwp_relative_pointer_manager_v1* zwp_relative_pointer_manager_v1 = {};
    struct zwp_pointer_constraints_v1* zwp_pointer_constraints_v1 = {};

    struct zwp_linux_dmabuf_v1* zwp_linux_dmabuf_v1 = {};
    std::vector<std::pair<gpu::Format, gpu::DrmModifier>> format_table = {};
    gpu::FormatSet format_set;

    struct wp_linux_drm_syncobj_manager_v1* wp_linux_drm_syncobj_manager_v1 = {};
    wroc_wl_proxy_cache<gpu::Semaphore, wp_linux_drm_syncobj_timeline_v1> syncobj_cache { wp_linux_drm_syncobj_timeline_v1_destroy };
    wroc_wl_proxy_cache<gpu::Image, wl_buffer> buffer_cache { wl_buffer_destroy };

    struct wl_seat* wl_seat = {};

    u32 next_window_id = 1;
    std::vector<core::Ref<wroc_wayland_output>> outputs;

    core::Ref<wroc_wayland_keyboard> keyboard = {};
    core::Ref<wroc_wayland_pointer>  pointer = {};

    core::Fd wl_event_source_fd;

    std::chrono::steady_clock::time_point current_dispatch_time;

    virtual void init() final override;
    virtual void start() final override;

    virtual const gpu::FormatSet& get_output_format_set() final override;

    virtual void create_output() final override;
    virtual void destroy_output(wroc_output*) final override;

    ~wroc_wayland_backend();
};

wroc_wayland_output* wroc_wayland_backend_find_output_for_surface(wroc_wayland_backend*, wl_surface*);

void wroc_wayland_backend_update_pointer_constraint(wroc_wayland_output*);

extern const xdg_surface_listener wroc_xdg_surface_listener;
extern const xdg_wm_base_listener wroc_xdg_wm_base_listener;
extern const wl_pointer_listener wroc_wl_pointer_listener;
extern const wl_keyboard_listener wroc_wl_keyboard_listener;
extern const wl_seat_listener wroc_wl_seat_listener;
extern const wl_registry_listener wroc_wl_registry_listener;
extern const zxdg_toplevel_decoration_v1_listener wroc_zxdg_toplevel_decoration_v1_listener;
extern const xdg_toplevel_listener wroc_xdg_toplevel_listener;
extern const zwp_relative_pointer_v1_listener wroc_zwp_relative_pointer_v1_listener;
extern const zwp_locked_pointer_v1_listener wroc_zwp_locked_pointer_v1_listener;
extern const zwp_linux_dmabuf_feedback_v1_listener wroc_zwp_linux_dmabuf_feedback_v1_listener;
