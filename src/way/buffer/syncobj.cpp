#include "../surface/surface.hpp"

static
void get_surface(wl_client* client, wl_resource* manager, u32 id, wl_resource* wl_surface)
{
    auto* surface = way_get_userdata<WaySurface>(wl_surface);

    way_resource_create_refcounted(wp_linux_drm_syncobj_surface_v1, client, manager, id, surface);
}

static
void import_timeline(wl_client* client, wl_resource* resource, u32 id, fd_t syncobj_fd)
{
    auto _ = Fd(syncobj_fd);

    auto* server = way_get_userdata<WayServer>(resource);
    auto timeline = ref_create<WayTimeline>();
    timeline->resource = way_resource_create_refcounted(wp_linux_drm_syncobj_timeline_v1, client, resource, id, timeline.get());
    timeline->syncobj = gpu_syncobj_import(server->gpu, syncobj_fd);
}

WAY_INTERFACE(wp_linux_drm_syncobj_manager_v1) = {
    .destroy = way_simple_destroy,
    .get_surface = get_surface,
    .import_timeline = import_timeline,
};

WAY_BIND_GLOBAL(wp_linux_drm_syncobj_manager_v1, bind)
{
    way_resource_create_unsafe(wp_linux_drm_syncobj_manager_v1, bind.client, bind.version, bind.id, static_cast<WayServer*>(bind.data));
}

WAY_INTERFACE(wp_linux_drm_syncobj_timeline_v1) = {
    .destroy = way_simple_destroy,
};

static
void set_acquire_point(wl_client* client, wl_resource* resource, wl_resource* _timeline, u32 point_hi, u32 point_lo)
{
    auto* surface = way_get_userdata<WaySurface>(resource);
    auto* timeline = way_get_userdata<WayTimeline>(_timeline);

    surface->pending->acquire_point = {
        .syncobj = timeline->syncobj,
        .value = u64(point_hi) << 32 | point_lo,
    };
}

static
void set_release_point(wl_client* client, wl_resource* resource, wl_resource* _timeline, u32 point_hi, u32 point_lo)
{
    auto* surface = way_get_userdata<WaySurface>(resource);
    auto* timeline = way_get_userdata<WayTimeline>(_timeline);

    surface->pending->release_point = {
        .syncobj = timeline->syncobj,
        .value = u64(point_hi) << 32 | point_lo,
    };
}

WAY_INTERFACE(wp_linux_drm_syncobj_surface_v1) = {
    .destroy = way_simple_destroy,
    .set_acquire_point = set_acquire_point,
    .set_release_point = set_release_point,
};
