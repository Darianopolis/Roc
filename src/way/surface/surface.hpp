#pragma once

#include "state.hpp"
#include "region.hpp"

#include "../server.hpp"

#include <gpu/gpu.hpp>

#include <wayland/server/viewporter.h>
#include <wayland/server/xdg-decoration-unstable-v1.h>
#include <wayland/server/linux-drm-syncobj-v1.h>
#include <wayland/server/server-decoration.h>

struct WaySurface;
struct WayClient;

// -----------------------------------------------------------------------------

struct WaySubsurface;
struct WaySurfaceTree;
struct WayCursorSurface;
struct WayXdgSurface;
struct WayToplevel;
struct WayPopup;

// -----------------------------------------------------------------------------

struct WayTimeline : WayObject
{
    WayResource     resource;
    Ref<GpuSyncobj> syncobj;
};

struct WayTimelinePoint
{
    Ref<GpuSyncobj> syncobj;
    u64             value;
};

// -----------------------------------------------------------------------------

enum class WaySurfaceStateComponent : u32
{
    // wl_surface
    buffer           = 1 << 0,
    opaque_region    = 1 << 1,
    input_region     = 1 << 2,
    buffer_transform = 1 << 3,
    buffer_scale     = 1 << 4,

    // wp_viewport
    buffer_source      = 1 << 5,
    buffer_destination = 1 << 6,
};

struct WayPositioner;
struct WayBuffer;

static constexpr aabb2f32 way_infinite_aabb = {{-INFINITY, -INFINITY}, {INFINITY, INFINITY}, minmax};

struct WaySurfaceState
{
    Flags<WaySurfaceStateComponent> set;
    WayCommitId commit;

    struct {
        WayResourceList frame_callbacks;
        vec2i32         offset;
        region2f32      opaque_region;
        region2f32      input_region = {way_infinite_aabb};
        WayDamageRegion damage;
    } surface;

    Ref<WayBuffer>      buffer;
    Ref<GpuImage>       image;
    wl_output_transform buffer_transform;
    i32                 buffer_scale = 1;
    rect2f32            buffer_source;
    vec2i32             buffer_destination;
    WayDamageRegion     buffer_damage;

    WayTimelinePoint acquire_point;
    WayTimelinePoint release_point;

    ~WaySurfaceState();
};

struct WaySurface : WayObject
{
    WayClient* client;

    Weak<WaySurface> parent;

    // core
    WayResource resource;
    WaySurfaceRole role = WaySurfaceRole::none;

    // state tracking
    WayCommitId last_commit_id;
    Ref<WaySurfaceState> pending = ref_create<WaySurfaceState>();
    std::deque<Ref<WaySurfaceState>> cached;
    WaySurfaceState current;

    // scene
    struct {
        Ref<SceneTree>        tree;
        Ref<SceneTexture>     texture;
        Ref<SceneInputRegion> input_region;
        Ref<SeatFocus>        focus;
    } scene;

    bool mapped;

    WayXdgSurface* xdg;
    WayToplevel*   toplevel;
    WayPopup*      popup;
    WaySubsurface* subsurface;

    Ref<WaySurfaceTree>   tree;
    Ref<WayCursorSurface> cursor_role;

    std::vector<WaySurfaceAddon*> addons;

    ~WaySurface();
};

void way_surface_addon_register(WaySurface*, WaySurfaceAddon*);

void way_surface_on_redraw(WaySurface*);

void way_viewport_apply(WaySurface*, WaySurfaceState& from);

// -----------------------------------------------------------------------------

struct WaySurfaceTreePlace
{
    Ref<WaySurface> reference;
    Ref<WaySurface> surface;
    bool above;
};

struct WaySurfaceTreeMove
{
    Ref<WaySurface> surface;
    vec2i32 position;
};

struct WaySurfaceStateRequest
{
    WayCommitId commit;

    std::vector<WaySurfaceTreePlace> places;
    std::vector<WaySurfaceTreeMove>  moves;
};

struct WaySurfaceTree : WaySurfaceAddon
{
    WayCommitQueue<WaySurfaceStateRequest> queue;

    virtual void commit(WayCommitId) final override;
    virtual void apply( WayCommitId) final override;
};

struct WaySubsurfaceStateRequest
{
    WayCommitId commit;

    Weak<WaySurface> parent;
    WayCommitId      parent_commit;
};

struct WaySubsurface : WaySurfaceAddon
{
    WayCommitQueue<WaySubsurfaceStateRequest> queue;

    WayResource resource;

    bool synchronized;

    virtual void commit(WayCommitId)         final override;
    virtual auto test(  WayCommitId) -> bool final override;
    virtual void apply( WayCommitId)         final override;

    ~WaySubsurface();
};

// -----------------------------------------------------------------------------

WAY_INTERFACE_DECLARE(wl_compositor, 6);
WAY_INTERFACE_DECLARE(wl_region);
WAY_INTERFACE_DECLARE(wl_surface);

WAY_INTERFACE_DECLARE(wl_subcompositor, 1);
WAY_INTERFACE_DECLARE(wl_subsurface);

WAY_INTERFACE_DECLARE(wp_viewporter, 1);
WAY_INTERFACE_DECLARE(wp_viewport);

WAY_INTERFACE_DECLARE(zxdg_decoration_manager_v1, 1);
WAY_INTERFACE_DECLARE(zxdg_toplevel_decoration_v1);

WAY_INTERFACE_DECLARE(org_kde_kwin_server_decoration_manager, 1);
WAY_INTERFACE_DECLARE(org_kde_kwin_server_decoration);

WAY_INTERFACE_DECLARE(wp_linux_drm_syncobj_manager_v1, 1);
WAY_INTERFACE_DECLARE(wp_linux_drm_syncobj_timeline_v1);
WAY_INTERFACE_DECLARE(wp_linux_drm_syncobj_surface_v1);
