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

UNIX_ERROR_BEHAVIOUR(wl_event_loop_dispatch, negative_one)

struct WayClient;
struct WaySurface;
struct WayDataOffer;
struct WaySeat;

// -----------------------------------------------------------------------------

struct WayKeymap
{
    Fd fd;
    u32 size;
};

// -----------------------------------------------------------------------------

struct WayServer : WayObject
{
    ExecContext* exec;

    std::chrono::steady_clock::time_point epoch;

    Gpu* gpu;
    Scene*  scene;
    SceneSystemId scene_system;

    Ref<SceneClient> seat_listener;

    wl_display* wl_display;
    std::string socket_name;

    Ref<GpuSampler> sampler;

    RefVector<WaySeat> seats;

    struct {
        WayListener created;
    } client;

    struct {
        Fd  format_table;
        usz format_table_size;
        std::vector<u16> tranche_formats;
    } dmabuf;

    ~WayServer();
};

auto way_get_elapsed(WayServer*) -> std::chrono::steady_clock::duration;

// -----------------------------------------------------------------------------

struct WaySeat : WayObject
{
    WayServer* server;
    SceneSeat* SceneSeat;

    wl_global* global;

    struct {
        SceneKeyboard* scene;
        WayKeymap keymap;
    } keyboard;

    struct {
        ScenePointer* scene;
    } pointer;

    struct {
        Weak<WaySurface> pointer;
        Weak<WaySurface> keyboard;
    } focus;

    ~WaySeat();
};

struct WaySeatClient : WayObject
{
    WaySeat* seat;
    WayClient* client;

    WayResourceList keyboards;
    WayResourceList pointers;
    WayResourceList data_devices;

    struct {
        Ref<WayDataOffer> offer;
    } drag;

    ~WaySeatClient();
};

// -----------------------------------------------------------------------------

void way_seat_init(WayServer*);

void way_seat_on_keyboard_enter(WaySeatClient*, SceneEvent*);
void way_seat_on_keyboard_leave(WaySeatClient*, SceneEvent*);
void way_seat_on_key(           WaySeatClient*, SceneEvent*);
void way_seat_on_modifier(      WaySeatClient*, SceneEvent*);

void way_seat_on_pointer_enter(WaySeatClient*, SceneEvent*);
void way_seat_on_pointer_leave(WaySeatClient*, SceneEvent*);
void way_seat_on_motion(       WaySeatClient*, SceneEvent*);
void way_seat_on_button(       WaySeatClient*, SceneEvent*);
void way_seat_on_scroll(       WaySeatClient*, SceneEvent*);

// -----------------------------------------------------------------------------

struct WayClient : WayObject
{
    WayServer* server;

    wl_client* wl_client;

    Ref<SceneClient> scene;

    std::vector<WaySurface*> surfaces;
    std::vector<WaySeatClient*> seat_clients;
};

void way_on_client_create(wl_listener*, void* data);

WayClient* way_client_from(WayServer*, const wl_client*);

auto way_client_is_behind(WayClient*) -> bool;

// -----------------------------------------------------------------------------

void way_dmabuf_init(WayServer*);

// -----------------------------------------------------------------------------

struct WayRegion : WayObject
{
    WayResource resource;

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
struct WayDamageRegion
{
    static constexpr aabb2i32 Empty = {{INT_MAX, INT_MAX}, {INT_MIN, INT_MIN}, minmax};

private:
    aabb2i32 region = Empty;

public:
    void damage(aabb2i32 damage)
    {
        region = aabb_outer(region, damage);
    }

    void clip_to(aabb2i32 limit)
    {
        region = aabb_inner(region, limit);
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
        debug_assert(*this);
        return region;
    }
};

// -----------------------------------------------------------------------------

struct WaySurface;
struct WayBuffer;

// -----------------------------------------------------------------------------

namespace detail { struct WayCommitIdFingerprint {}; }
using WayCommitId = UniqueInteger<u32, detail::WayCommitIdFingerprint>;

namespace detail { struct WaySerialFingerprint {}; }
using WaySerial = UniqueInteger<u32, detail::WaySerialFingerprint>;

// -----------------------------------------------------------------------------

enum class WaySurfaceRole : u32
{
    none,
    cursor,
    drag_icon,
    subsurface,
    xdg_toplevel,
    xdg_popup,
};

// -----------------------------------------------------------------------------

template<typename Enum>
struct WayState
{
    WayCommitId commit;

    struct {
        u32 set   = 0;
        u32 unset = 0;
    } committed;

    static_assert(sizeof(u32) * CHAR_BIT >
        std::to_underlying(*std::ranges::max_element(magic_enum::enum_values<Enum>())));

    bool is_set(  Enum state) const { return committed.set   & (1 << std::to_underlying(state)); }
    bool is_unset(Enum state) const { return committed.unset & (1 << std::to_underlying(state)); }

    bool empty() const { return !committed.set && !committed.unset; }

    void set(Enum state)
    {
        committed.set   |=  (1 << std::to_underlying(state));
        committed.unset &= ~(1 << std::to_underlying(state));
    }

    void unset(Enum state)
    {
        committed.unset |=  (1 << std::to_underlying(state));
        committed.set   &= ~(1 << std::to_underlying(state));
    }
};

template<typename T>
struct WayStateQueue
{
    Ref<T> pending;
    std::deque<Ref<T>> cached;

    WayStateQueue()
    {
        pending = ref_create<T>();
    }

    T* commit(WayCommitId id)
    {
        if (pending->empty()) {
            return nullptr;
        }
        pending->id = id;
        auto* prev_pending = pending.get();
        cached.emplace_back(std::move(pending));
        pending = ref_create<T>();
        return prev_pending;
    }

    template<typename ApplyFn>
    void apply(WayCommitId id, ApplyFn&& apply_fn)
    {
        while (cached.empty()) {
            auto& packet = cached.front();
            if (packet.id > id) break;
            apply_fn(packet.state, packet.id);
            cached.pop_front();
        }
    }
};

// -----------------------------------------------------------------------------

enum class WaySurfaceStateComponent : u32
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

struct WaySubsurfacePlace
{
    Ref<WaySurface> reference;
    Ref<WaySurface> subsurface;
    bool above;
};

struct WaySubsurfaceMove
{
    Ref<WaySurface> subsurface;
    vec2i32 position;
};

static constexpr aabb2f32 way_infinite_aabb = {{-INFINITY, -INFINITY}, {INFINITY, INFINITY}, minmax};

struct WayPositioner;

struct WaySurfaceState : WayState<WaySurfaceStateComponent>
{
    struct {
        WayCommitId commit;
    } parent;

    struct {
        WayResourceList frame_callbacks;
        vec2i32 offset;
        region2f32 opaque_region;
        region2f32 input_region;
        WayDamageRegion damage;
    } surface;

    Ref<WayBuffer>      buffer;
    Ref<GpuImage>       image;
    wl_output_transform buffer_transform;
    i32                 buffer_scale = 1;
    rect2f32            buffer_source;
    vec2i32             buffer_destination;
    WayDamageRegion     buffer_damage;

    struct {
        rect2i32 geometry;
        WaySerial acked_serial;
    } xdg;

    struct {
        std::vector<WaySubsurfacePlace> places;
        std::vector<WaySubsurfaceMove>  moves;
    } subsurface;

    struct {
        vec2i32 min_size;
        vec2i32 max_size;
        std::string title;
        std::string app_id;
    } toplevel;

    ~WaySurfaceState();
};

struct WaySurface : WayObject
{
    WayClient* client;

    Weak<WaySurface> parent;

    // core
    WayResource wl_surface;
    WaySurfaceRole role = WaySurfaceRole::none;

    // state tracking
    WayCommitId last_commit_id;
    WayStateQueue<WaySurfaceState> queue;
    WaySurfaceState current;

    // wl_subsurface
    struct {
        WayResource resource;
        bool synchronized;
    } subsurface;

    // xdg_surface
    WayResource xdg_surface;
    WaySerial sent_serial;
    WaySerial acked_serial;

    // xdg_popup
    struct {
        WayResource resource;
        vec2f32 position;
    } popup;

    // xdg_toplevel
    struct {
        WayResource resource;
        rect2f32 anchor;
        vec2f32 gravity = {1, 1};
        Ref<SceneWindow> window;

        WaySerial pending; // commit response to resize configure is pending
        bool queued;  // new reposition request received while pending
    } toplevel;

    // scene
    struct {
        Ref<SceneTree> tree;
        Ref<SceneTexture> texture;
        Ref<SceneInputRegion> input_region;
    } scene;

    bool mapped;

    ~WaySurface();
};

void way_role_destroy(wl_client*, wl_resource*);

void way_surface_on_redraw(WaySurface*);

void way_viewport_apply(WaySurface*, WaySurfaceState& from);

// -----------------------------------------------------------------------------

void way_subsurface_commit(WaySurface*, WaySurfaceState&);
void way_subsurface_apply( WaySurface*, WaySurfaceState&);

// -----------------------------------------------------------------------------

void way_xdg_surface_apply(WaySurface*, WaySurfaceState&);
void way_xdg_surface_configure(WaySurface*);

// -----------------------------------------------------------------------------

void way_toplevel_apply(        WaySurface*, WaySurfaceState&);
void way_toplevel_on_map_change(WaySurface*, bool mapped);
void way_toplevel_on_reposition(WaySurface*, rect2f32 frame, vec2f32 gravity);
void way_toplevel_on_close(     WaySurface*);

// -----------------------------------------------------------------------------

void way_create_positioner(wl_client*, wl_resource*, u32 id);
void way_get_popup(        wl_client*, wl_resource*, u32 id,
			               wl_resource* parent, wl_resource* positioner);

void way_popup_apply(WaySurface*, WaySurfaceState&);

// -----------------------------------------------------------------------------

void way_output_init(WayServer*);

// -----------------------------------------------------------------------------

struct WayDataSource : WayObject
{
    WaySeatClient* seat_client;

    WayResource resource;

    Ref<SceneDataSource> source;
};

struct WayDataOffer : WayObject
{
    WaySeatClient* seat_client;

    WayResource resource;

    Ref<SceneDataSource> source;
};

void way_data_offer_selection(WaySeatClient*);

// -----------------------------------------------------------------------------

struct WayBuffer : WayObject
{
    vec2u32 extent;

    // Sent on apply, should return a GpuImage when the buffer is ready to display
    [[nodiscard]] virtual auto acquire(WaySurface*, WaySurfaceState& from) -> Ref<GpuImage> = 0;

protected:
    ~WayBuffer() = default;
};

// -----------------------------------------------------------------------------

struct WayShmPool;

struct WayShmPool : WayObject
{
    WayServer* server;

    WayResource resource;

    Fd    fd;
    void* data;
    usz   size;

    ~WayShmPool();
};

// -----------------------------------------------------------------------------

#define WAY_ADDON_SIMPLE_STATE_REQUEST(Type, Field, Name, Expr, ...) \
    [](wl_client* client, wl_resource* resource, __VA_ARGS__) { \
        auto* surface = way_get_userdata<WaySurface>(resource); \
        surface->queue.pending->Field = Expr; \
        surface->queue.pending->set(WaySurfaceStateComponent::Name); \
    }

/**
 * Convenience macro for applying trivial state elements.
 */
#define WAY_ADDON_SIMPLE_STATE_APPLY(From, To, Field, Name) \
    do { \
        if ((From).is_set(WaySurfaceStateComponent::Name)) { \
            (To).Field = std::move((From).Field); \
        } \
    } while (false)

// -----------------------------------------------------------------------------

auto way_next_serial(WayServer* server) -> WaySerial;

void way_queue_client_flush(WayServer* server);

void way_send_(WayServer* server, const char* fn_name, auto fn, auto&& resource, auto&&... args)
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
void way_post_error(WayServer* server, wl_resource* resource, u32 code, std::format_string<Args...> fmt, Args&&... args)
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

wl_global* way_global_(WayServer*, const wl_interface*, i32 version, wl_global_bind_func_t, WayObject* data = nullptr);
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

struct WayBindGlobalData
{
    wl_client* client;
    void*      data;
    u32        version;
    u32        id;
};

#define WAY_BIND_GLOBAL(Name, Data) \
    static void way_##Name##_bind_global_impl(const WayBindGlobalData& Data); \
           void way_##Name##_bind_global(wl_client* client, void* data, u32 version, u32 id) \
    { \
        way_##Name##_bind_global_impl({client, data, version, id}); \
    } \
    static void way_##Name##_bind_global_impl(const WayBindGlobalData& Data)

#define WAY_INTERFACE_DECLARE(Name, ...) \
    extern WAY_INTERFACE(Name) \
    __VA_OPT__(; \
        static_assert(std::same_as<decltype(__VA_ARGS__), int>); \
        constexpr u32 way_##Name##_version = __VA_ARGS__; \
        void way_##Name##_bind_global(wl_client* client, void* data, u32 version, u32 id) \
    )

// -----------------------------------------------------------------------------

wl_resource* way_resource_create_(wl_client*, const wl_interface*, int version, int id, const void* impl, WayObject*, bool refcount);

inline
wl_resource* way_resource_create_(wl_client* client, const wl_interface* interface, wl_resource* parent, int id, const void* impl, WayObject* object, bool refcount)
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
