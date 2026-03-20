#pragma once

#include "way.hpp"
#include "util.hpp"

#include "core/math.hpp"

#include <wayland-server-core.h>
#include <wayland-server-protocol.h>

#include <wayland/server/xdg-shell.h>
#include <wayland/server/linux-dmabuf-v1.h>
#include <wayland/server/pointer-gestures-unstable-v1.h>
#include <wayland/server/viewporter.h>
#include <wayland/server/relative-pointer-unstable-v1.h>
#include <wayland/server/pointer-constraints-unstable-v1.h>
#include <wayland/server/xdg-decoration-unstable-v1.h>
#include <wayland/server/server-decoration.h>
#include <wayland/server/cursor-shape-v1.h>
#include <wayland/server/linux-drm-syncobj-v1.h>

CORE_UNIX_ERROR_BEHAVIOUR(wl_event_loop_dispatch, negative_one)

struct way_client;
struct way_surface;

// -----------------------------------------------------------------------------

struct way_keymap
{
    core_fd fd;
    u32     size;
};

// -----------------------------------------------------------------------------

struct way_server
{
    exec_context* exec;

    std::chrono::steady_clock::time_point epoch;

    gpu_context* gpu;
    scene_context*  scene;
    scene_system_id scene_system;

    wl_display* wl_display;
    std::string socket_name;

    ref<gpu_sampler> sampler;

    struct {
        way_listener created;
    } client;

    struct {
        scene_keyboard* scene;
        way_keymap keymap;
    } keyboard;

    struct {
        scene_pointer* scene;
    } pointer;

    struct {
        weak<way_surface> pointer;
        weak<way_surface> keyboard;
    } focus;

    struct {
        core_fd format_table;
        usz format_table_size;
        std::vector<u16> tranche_formats;
    } dmabuf;

    ~way_server();
};

auto way_get_elapsed(way_server*) -> std::chrono::steady_clock::duration;

// -----------------------------------------------------------------------------

void way_dmabuf_init(way_server*);

// -----------------------------------------------------------------------------

void way_seat_init(way_server* server);

void way_seat_on_keyboard_enter(way_client*, scene_event*);
void way_seat_on_keyboard_leave(way_client*, scene_event*);
void way_seat_on_key(           way_client*, scene_event*);
void way_seat_on_modifier(      way_client*, scene_event*);

void way_seat_on_pointer_enter(way_client*, scene_event*);
void way_seat_on_pointer_leave(way_client*, scene_event*);
void way_seat_on_motion(       way_client*, scene_event*);
void way_seat_on_button(       way_client*, scene_event*);
void way_seat_on_scroll(       way_client*, scene_event*);

// -----------------------------------------------------------------------------

struct way_region
{
    way_resource resource;

    region2f32 region;
};

// -----------------------------------------------------------------------------

/**
 * Represents accumulated damage for a surface or buffer.
 *
 * Accurate damage is not required for correctness, as reading from un-damaged parts of a buffer is valid.
 *
 * Taking advantage of this, and the fact that damage is usually localized, we only
 * track the outer bounds of accumulated damage. This avoids relatively expensive region operations,
 * and reduces the number of copies on transfer and clip regions during rendering; while remaining
 * optimal or near optimal for all realistic scenarios.
 */
struct way_damage_region
{
    static constexpr aabb2i32 Empty = {{INT_MAX, INT_MAX}, {INT_MIN, INT_MIN}, core_minmax};

private:
    aabb2i32 region = Empty;

public:
    void damage(aabb2i32 damage)
    {
        region = core_aabb_outer(region, damage);
    }

    void clip_to(aabb2i32 limit)
    {
        region = core_aabb_inner(region, limit);
    }

    void clear()
    {
        region = Empty;
    }

    explicit operator bool() const
    {
        return region.max.x > region.min.x
            && region.max.y > region.min.y;
    }

    aabb2i32 bounds()
    {
        core_assert(*this);
        return region;
    }
};

// -----------------------------------------------------------------------------

struct way_surface;
struct way_buffer;

// -----------------------------------------------------------------------------

using way_commit_id = u32;

enum class way_surface_role : u32
{
    none,
    cursor,
    drag_icon,
    subsurface,
    xdg_toplevel,
    xdg_popup,
};

// -----------------------------------------------------------------------------

enum class way_surface_committed_state : u32
{
    // wl_surface
    buffer,
    opaque_region,
    input_region,
    buffer_transform,
    buffer_scale,

    // wp_viewport
    buffer_source,
    buffer_destination,

    // wl_subsurface / xdg_toplevel / xdg_popup
    parent,
    parent_commit,

    // xdg_surface
    geometry,
    acked_serial,

    // xdg_toplevel
    title,
    app_id,
    min_size,
    max_size,
};

struct way_subsurface_place
{
    ref<way_surface> reference;
    ref<way_surface> subsurface;
    bool above;
};

struct way_subsurface_move
{
    ref<way_surface> subsurface;
    vec2i32 position;
};

static constexpr aabb2f32 way_infinite_aabb = {{-INFINITY, -INFINITY}, {INFINITY, INFINITY}, core_minmax};

struct way_positioner;

struct way_surface_state
{
    way_commit_id commit;

    struct {
        u32 set   = 0;
        u32 unset = 0;
    } committed;

    static_assert(sizeof(u32) * CHAR_BIT >
        std::to_underlying(*std::ranges::max_element(magic_enum::enum_values<way_surface_committed_state>())));

    bool is_set(  way_surface_committed_state state) { return committed.set   & (1 << std::to_underlying(state)); }
    bool is_unset(way_surface_committed_state state) { return committed.unset & (1 << std::to_underlying(state)); }

    void set(way_surface_committed_state state)
    {
        committed.set   |=  (1 << std::to_underlying(state));
        committed.unset &= ~(1 << std::to_underlying(state));
    }

    void unset(way_surface_committed_state state)
    {
        committed.unset |=  (1 << std::to_underlying(state));
        committed.set   &= ~(1 << std::to_underlying(state));
    }

    struct {
        way_commit_id commit;
    } parent;

    struct {
        way_resource_list frame_callbacks;
        vec2i32 offset;
        region2f32 opaque_region;
        region2f32 input_region;
        way_damage_region damage;
    } surface;


    ref<way_buffer>     buffer;
    ref<gpu_image>      image;
    wl_output_transform buffer_transform;
    i32                 buffer_scale = 1;
    rect2f32            buffer_source;
    vec2i32             buffer_destination;
    way_damage_region   buffer_damage;

    struct {
        rect2i32 geometry;
        u32 acked_serial;
    } xdg;

    struct {
        std::vector<way_subsurface_place> places;
        std::vector<way_subsurface_move>  moves;
    } subsurface;

    struct {
        vec2i32 min_size;
        vec2i32 max_size;
        std::string title;
        std::string app_id;
    } toplevel;

    ~way_surface_state();
};

struct way_surface
{
    way_client* client;

    weak<way_surface> parent;

    // core
    way_resource wl_surface;
    way_surface_role role = way_surface_role::none;

    // state tracking
    way_commit_id last_commit_id;
    way_surface_state* pending;
    std::deque<way_surface_state> cached;
    way_surface_state current;

    // wl_subsurface
    struct {
        way_resource resource;
        bool synchronized;
    } subsurface;

    // xdg_surface
    way_resource xdg_surface;
    u32 sent_serial;
    u32 acked_serial;

    // xdg_popup
    struct {
        way_resource resource;
        vec2f32      position;
    } popup;

    // xdg_toplevel
    struct {
        way_resource resource;
        rect2f32 anchor;
        vec2f32 gravity = {1, 1};
        ref<scene_window> window;

        bool pending; // commit response to resize configure is pending
        bool queued;  // new reposition request received while pending
    } toplevel;

    // scene
    struct {
        ref<scene_tree>         tree;
        ref<scene_texture>      texture;
        ref<scene_input_region> input_region;
    } scene;

    bool mapped;

    ~way_surface();
};

void way_role_destroy(wl_client*, wl_resource*);

void way_surface_on_redraw(way_surface*);

void way_viewport_apply(way_surface*, way_surface_state& from);

// -----------------------------------------------------------------------------

void way_subsurface_commit(way_surface*, way_surface_state&);
void way_subsurface_apply( way_surface*, way_surface_state&);

// -----------------------------------------------------------------------------

void way_xdg_surface_apply(way_surface*, way_surface_state&);
void way_xdg_surface_configure(way_surface*);

// -----------------------------------------------------------------------------

void way_toplevel_apply(        way_surface*, way_surface_state&);
void way_toplevel_on_map_change(way_surface*, bool mapped);
void way_toplevel_on_reposition(way_surface*, rect2f32 frame, vec2f32 gravity);
void way_toplevel_on_close(     way_surface*);

// -----------------------------------------------------------------------------

void way_create_positioner(wl_client*, wl_resource*, u32 id);
void way_get_popup(        wl_client*, wl_resource*, u32 id,
			               wl_resource* parent, wl_resource* positioner);

void way_popup_apply(way_surface*, way_surface_state&);

// -----------------------------------------------------------------------------

void way_output_init(way_server*);

// -----------------------------------------------------------------------------

struct way_data_source
{
    way_client* client;

    way_resource resource;

    ref<scene_data_source> source;
};

struct way_data_offer
{
    way_client* client;

    way_resource resource;

    ref<scene_data_source> source;
};

void way_data_offer_selection(way_client*);

// -----------------------------------------------------------------------------

struct way_buffer
{
    vec2u32 extent;

    // Sent on apply, should return a gpu_image when the buffer is ready to display
    [[nodiscard]] virtual auto acquire(way_surface*, way_surface_state& from) -> ref<gpu_image> = 0;

protected:
    ~way_buffer() = default;
};

// -----------------------------------------------------------------------------

struct way_shm_pool;

struct way_shm_pool
{
    way_server* server;

    way_resource resource;

    core_fd fd;
    void*   data;
    usz     size;

    ~way_shm_pool();
};

// -----------------------------------------------------------------------------

struct way_client
{
    way_server* server;

    wl_client* wl_client;

    ref<scene_client> scene;

    std::vector<way_surface*> surfaces;

    way_resource_list keyboards;
    way_resource_list pointers;
    way_resource_list data_devices;

    struct {
        ref<way_data_offer> offer;
    } drag;
};

void way_on_client_create(wl_listener*, void* data);

way_client* way_client_from(way_server*, const wl_client*);

auto way_client_is_behind(way_client*) -> bool;

// -----------------------------------------------------------------------------

#define WAY_ADDON_SIMPLE_STATE_REQUEST(Type, Field, Name, Expr, ...) \
    [](wl_client* client, wl_resource* resource, __VA_ARGS__) { \
        auto* surface = way_get_userdata<way_surface>(resource); \
        surface->pending->Field = Expr; \
        surface->pending->set(way_surface_committed_state::Name); \
    }

/**
 * Convenience macro for applying trivial state elements.
 */
#define WAY_ADDON_SIMPLE_STATE_APPLY(From, To, Field, Name) \
    do { \
        if ((From).is_set(way_surface_committed_state::Name)) { \
            (To).Field = std::move((From).Field); \
        } \
    } while (false)

// -----------------------------------------------------------------------------

u32 way_next_serial(way_server* server);

void way_queue_client_flush(way_server* server);

void way_send_(way_server* server, const char* fn_name, auto fn, auto&& resource, auto&&... args)
{
    if (resource) {
        fn(resource, args...);
        way_queue_client_flush(server);
    } else {
        log_error("Failed to dispatch {}, resource is null", fn_name);
    }
}

#define way_send(Server, Fn, Resource, ...) \
    way_send_(Server, #Fn, Fn, Resource __VA_OPT__(,) __VA_ARGS__)

template<typename ...Args>
void way_post_error(way_server* server, wl_resource* resource, u32 code, std::format_string<Args...> fmt, Args&&... args)
{
    if (!resource) return;
    auto message = std::vformat(fmt.get(), std::make_format_args(args...));
    log_error("{}", message);
    wl_resource_post_error(resource, code, "%s", message.c_str());
    way_queue_client_flush(server);
}

// -----------------------------------------------------------------------------

void way_simple_destroy(wl_client* client, wl_resource* resource);

// -----------------------------------------------------------------------------

wl_global* way_global_(way_server*, const wl_interface*, i32 version, wl_global_bind_func_t, void* data = nullptr);
#define way_global(Server, Interface, ...) \
    way_global_(Server, &Interface##_interface, way_##Interface##_version, way_##Interface##_bind_global __VA_OPT__(,) __VA_ARGS__)

// -----------------------------------------------------------------------------

#define WAY_STUB(Name) \
    .Name = [](wl_client*, wl_resource* resource, auto...) { \
        log_error("TODO - {}{{{}}}::" #Name, wl_resource_get_interface(resource)->name, (void*)resource); \
    }
#define WAY_STUB_QUIET(Name) \
    .Name = [](auto...) {}

#define WAY_INTERFACE(Name) \
    const struct Name##_interface way_##Name##_impl

struct way_bind_global_data
{
    wl_client* client;
    way_server* server;
    u32 version;
    u32 id;
};

#define WAY_BIND_GLOBAL(Name, Data) \
    static void way_##Name##_bind_global_impl(const way_bind_global_data& Data); \
           void way_##Name##_bind_global(wl_client* client, void* data, u32 version, u32 id) \
    { \
        way_##Name##_bind_global_impl({client, way_get_userdata<way_server>(data), version, id}); \
    } \
    static void way_##Name##_bind_global_impl(const way_bind_global_data& Data)

#define WAY_INTERFACE_DECLARE(Name, ...) \
    extern WAY_INTERFACE(Name) \
    __VA_OPT__(; \
        static_assert(std::same_as<decltype(__VA_ARGS__), int>); \
        constexpr u32 way_##Name##_version = __VA_ARGS__; \
        void way_##Name##_bind_global(wl_client* client, void* data, u32 version, u32 id) \
    )

// -----------------------------------------------------------------------------

wl_resource* way_resource_create_(wl_client*, const wl_interface*, int version, int id, const void* impl, void*, bool refcount);

inline
wl_resource* way_resource_create_(wl_client* client, const wl_interface* interface, wl_resource* parent, int id, const void* impl, void* object, bool refcount)
{
    return way_resource_create_(client, interface, wl_resource_get_version(parent), id, impl, object, refcount);
}

#define way_resource_create_unsafe(Name, Client, Version, IdOrResource, Object) \
    way_resource_create_(Client, &Name##_interface, Version, IdOrResource, &way_##Name##_impl, Object, false)

#define way_resource_create_refcounted(Name, Client, Version, IdOrResource, Object) \
    way_resource_create_(Client, &Name##_interface, Version, IdOrResource, &way_##Name##_impl, Object, true)

// -----------------------------------------------------------------------------

WAY_INTERFACE_DECLARE(wl_compositor, 6);
WAY_INTERFACE_DECLARE(wl_region);
WAY_INTERFACE_DECLARE(wl_surface);

WAY_INTERFACE_DECLARE(wl_subcompositor, 1);
WAY_INTERFACE_DECLARE(wl_subsurface);

WAY_INTERFACE_DECLARE(wl_output, 4);

WAY_INTERFACE_DECLARE(wl_data_device_manager, 3);
WAY_INTERFACE_DECLARE(wl_data_offer);
WAY_INTERFACE_DECLARE(wl_data_source);
WAY_INTERFACE_DECLARE(wl_data_device);

WAY_INTERFACE_DECLARE(zwp_pointer_gestures_v1, 3);
WAY_INTERFACE_DECLARE(zwp_pointer_gesture_swipe_v1);
WAY_INTERFACE_DECLARE(zwp_pointer_gesture_pinch_v1);
WAY_INTERFACE_DECLARE(zwp_pointer_gesture_hold_v1);

WAY_INTERFACE_DECLARE(wp_cursor_shape_manager_v1, 2);
WAY_INTERFACE_DECLARE(wp_cursor_shape_device_v1);

WAY_INTERFACE_DECLARE(wp_viewporter, 1);
WAY_INTERFACE_DECLARE(wp_viewport);

WAY_INTERFACE_DECLARE(xdg_wm_base, 7);
WAY_INTERFACE_DECLARE(xdg_surface);
WAY_INTERFACE_DECLARE(xdg_toplevel);
WAY_INTERFACE_DECLARE(xdg_positioner);
WAY_INTERFACE_DECLARE(xdg_popup);

WAY_INTERFACE_DECLARE(wl_buffer);

WAY_INTERFACE_DECLARE(wl_shm, 2);
WAY_INTERFACE_DECLARE(wl_shm_pool);

WAY_INTERFACE_DECLARE(wl_seat, 9);
WAY_INTERFACE_DECLARE(wl_keyboard);
WAY_INTERFACE_DECLARE(wl_pointer);

WAY_INTERFACE_DECLARE(zwp_linux_dmabuf_v1, 5);
WAY_INTERFACE_DECLARE(zwp_linux_buffer_params_v1);
WAY_INTERFACE_DECLARE(zwp_linux_dmabuf_feedback_v1);

WAY_INTERFACE_DECLARE(zxdg_decoration_manager_v1, 1);
WAY_INTERFACE_DECLARE(zxdg_toplevel_decoration_v1);

WAY_INTERFACE_DECLARE(org_kde_kwin_server_decoration_manager, 1);
WAY_INTERFACE_DECLARE(org_kde_kwin_server_decoration);
