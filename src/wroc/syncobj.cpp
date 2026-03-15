#include "protocol.hpp"

const u32 wroc_wp_linux_drm_syncobj_manager_v1_version = 1;

static
void syncobj_get_surface(wl_client* client, wl_resource* resource, u32 id, wl_resource* _surface)
{
    auto* surface = wroc_get_userdata<wroc_surface>(_surface);

    auto* syncobj_surface_resource = wroc_resource_create(client, &wp_linux_drm_syncobj_surface_v1_interface, wl_resource_get_version(resource), id);
    auto syncobj_surface = core::create_unsafe<wroc_syncobj_surface>();
    syncobj_surface->resource = syncobj_surface_resource;
    wroc_surface_put_addon(surface, syncobj_surface);
    wroc_resource_set_implementation_refcounted(syncobj_surface_resource, &wroc_wp_linux_drm_syncobj_surface_v1_impl, syncobj_surface);
}

static
void syncobj_import_timeline(wl_client* client, wl_resource* resource, u32 id, int fd)
{
    auto* timeline_resource = wroc_resource_create(client, &wp_linux_drm_syncobj_timeline_v1_interface, wl_resource_get_version(resource), id);
    auto timeline = core::create_unsafe<wroc_syncobj_timeline>();
    timeline->resource = timeline_resource;
    timeline->syncobj = gpu::semaphore::import_syncobj(server->gpu, fd);
    wroc_resource_set_implementation_refcounted(timeline_resource, &wroc_wp_linux_drm_syncobj_timeline_v1_impl, timeline);
}

const struct wp_linux_drm_syncobj_manager_v1_interface wroc_wp_linux_drm_syncobj_manager_v1_impl = {
    .destroy = wroc_simple_resource_destroy_callback,
    .get_surface = syncobj_get_surface,
    .import_timeline = syncobj_import_timeline,
};

void wroc_wp_linux_drm_syncobj_manager_v1_bind_global(wl_client* client, void* data, u32 version, u32 id)
{
    log_debug("wroc_wp_linux_drm_syncobj_manager_v1_bind_global");
    auto resource = wroc_resource_create(client, &wp_linux_drm_syncobj_manager_v1_interface, version, id);
    wroc_resource_set_implementation(resource, &wroc_wp_linux_drm_syncobj_manager_v1_impl, nullptr);
}

// -----------------------------------------------------------------------------

const struct wp_linux_drm_syncobj_timeline_v1_interface wroc_wp_linux_drm_syncobj_timeline_v1_impl = {
    .destroy = wroc_simple_resource_destroy_callback,
};

// -----------------------------------------------------------------------------

void set_acquire_point(wl_client* client, wl_resource* resource, wl_resource* timeline, u32 point_hi, u32 point_lo)
{
    u64 point = u64(point_hi) << 32 | point_lo;
    auto surface = wroc_get_userdata<wroc_syncobj_surface>(resource);
    surface->acquire_timeline = wroc_get_userdata<wroc_syncobj_timeline>(timeline)->syncobj;
    surface->acquire_point = point;
}

void set_release_point(wl_client* client, wl_resource* resource, wl_resource* timeline, u32 point_hi, u32 point_lo)
{
    u64 point = u64(point_hi) << 32 | point_lo;
    auto surface = wroc_get_userdata<wroc_syncobj_surface>(resource);
    surface->release_timeline = wroc_get_userdata<wroc_syncobj_timeline>(timeline)->syncobj;
    surface->release_point = point;
}

const struct wp_linux_drm_syncobj_surface_v1_interface wroc_wp_linux_drm_syncobj_surface_v1_impl = {
    .destroy = wroc_simple_resource_destroy_callback,
    .set_acquire_point = set_acquire_point,
    .set_release_point = set_release_point,
};
