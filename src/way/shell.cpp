#include "internal.hpp"

static
void get_xdg_surface(wl_client* client, wl_resource* resource, u32 id, wl_resource* wl_surface)
{
    auto* surface = way_get_userdata<way_surface>(wl_surface);
    surface->xdg_surface = way_resource_create_refcounted(xdg_surface, client, resource, id, surface);
}

WAY_INTERFACE(xdg_wm_base) = {
    .destroy = way_simple_destroy,
    .create_positioner = way_create_positioner,
    .get_xdg_surface = get_xdg_surface,
    WAY_STUB(pong),
};

WAY_BIND_GLOBAL(xdg_wm_base)
{
    way_resource_create_unsafe(xdg_wm_base, client, version, id, way_get_userdata<way_server>(data));
}

// -----------------------------------------------------------------------------

static
void get_toplevel(wl_client* client, wl_resource* resource, u32 id)
{
    auto* surface = way_get_userdata<way_surface>(resource);
    surface->role = way_surface_role::xdg_toplevel;
    surface->toplevel.resource = way_resource_create_refcounted(xdg_toplevel, client, resource, id, surface);

    surface->toplevel.window = scene_window_create(surface->client->scene.get());

    scene_node_set_transform(surface->scene.transform.get(), scene_window_get_transform(surface->toplevel.window.get()));
    scene_tree_place_above(scene_window_get_tree(surface->toplevel.window.get()), nullptr, surface->scene.tree.get());
}

static
void ack_configure(wl_client* client, wl_resource* resource, u32 serial)
{
    auto* surface = way_get_userdata<way_surface>(resource);
    auto* server = surface->client->server;

    if (serial > surface->sent_serial) {
        way_post_error(server, surface->xdg_surface, XDG_SURFACE_ERROR_INVALID_SERIAL,
            "Client acked configure {} which is higher than latest sent configure serial {}",
            serial, surface->sent_serial);
        return;
    }

    if (serial <= surface->acked_serial) {
        log_warn("Client acked old configure serial");
        return;
    }

    surface->pending->xdg.acked_serial = serial;
    surface->pending->set(way_surface_committed_state::acked_serial);

    surface->acked_serial = serial;
}

WAY_INTERFACE(xdg_surface) = {
    WAY_STUB(destroy),
    .get_toplevel = get_toplevel,
    .get_popup = way_get_popup,
    .set_window_geometry = WAY_ADDON_SIMPLE_STATE_REQUEST(way_xdg_surface, xdg.geometry, geometry, rect2i32({x, y}, {w, h}, core_xywh), i32 x, i32 y, i32 w, i32 h),
    .ack_configure = ack_configure,
};

void way_xdg_surface_apply(way_surface* surface, way_surface_state& from)
{
    WAY_ADDON_SIMPLE_STATE_APPLY(from, surface->current, xdg.geometry,     geometry);
    WAY_ADDON_SIMPLE_STATE_APPLY(from, surface->current, xdg.acked_serial, acked_serial);

    if (!surface->current.is_set(way_surface_committed_state::geometry) && surface->mapped) {
        surface->current.xdg.geometry = { {}, surface->current.buffer.handle->extent, core_xywh };
    }
}

void way_xdg_surface_configure(way_surface* surface)
{
    auto* server = surface->client->server;
    surface->sent_serial = way_next_serial(server);
    way_send(server, xdg_surface_send_configure, surface->xdg_surface, surface->sent_serial);
}

// -----------------------------------------------------------------------------

void way_toplevel_on_map_change(way_surface* surface, bool mapped)
{
    if (mapped) {
        scene_window_map(surface->toplevel.window.get());
    } else {
        scene_window_unmap(surface->toplevel.window.get());
    }
}

static
void configure_toplevel(way_surface* surface, vec2u32 extent)
{
    way_send(surface->client->server, xdg_toplevel_send_configure, surface->toplevel.resource,
        extent.x, extent.y,
        ptr_to(way_to_wl_array<const xdg_toplevel_state>({
            XDG_TOPLEVEL_STATE_ACTIVATED,
        }))
    );
}

void way_toplevel_on_reposition(way_surface* surface, rect2f32 frame, vec2f32 gravity)
{
    if (surface->toplevel.anchor.extent == frame.extent) {
        // Move
        scene_window_set_frame(surface->toplevel.window.get(), {
            frame.origin,
            scene_window_get_frame(surface->toplevel.window.get()).extent,
            core_xywh
        });
    } else {
        // Resize
        if (surface->toplevel.pending) {
            surface->toplevel.queued = true;
        } else {
            configure_toplevel(surface, frame.extent);
            way_xdg_surface_configure(surface);
            surface->toplevel.pending = true;
        }
    }
    surface->toplevel.anchor = frame;
    surface->toplevel.gravity = gravity;
}

static
void send_premap_configure(way_surface* surface)
{
    auto* server = surface->client->server;

    if (wl_resource_get_version(surface->toplevel.resource) >= XDG_TOPLEVEL_WM_CAPABILITIES_SINCE_VERSION) {
        way_send(server, xdg_toplevel_send_wm_capabilities, surface->toplevel.resource, ptr_to(way_to_wl_array<const xdg_toplevel_wm_capabilities>({
            XDG_TOPLEVEL_WM_CAPABILITIES_FULLSCREEN,
        })));
    }

    configure_toplevel(surface, {0, 0});
    way_xdg_surface_configure(surface);
}

void way_toplevel_apply(way_surface* surface, way_surface_state& from)
{
    WAY_ADDON_SIMPLE_STATE_APPLY(from, surface->current, toplevel.title, title);
    WAY_ADDON_SIMPLE_STATE_APPLY(from, surface->current, toplevel.app_id, app_id);
    WAY_ADDON_SIMPLE_STATE_APPLY(from, surface->current, toplevel.max_size, max_size);
    WAY_ADDON_SIMPLE_STATE_APPLY(from, surface->current, toplevel.min_size, min_size);

    if (surface->mapped) {
        vec2f32 extent = surface->current.xdg.geometry.extent;
        rect2f32 anchor = surface->toplevel.anchor;

        rect2f32 frame { anchor.origin, extent, core_xywh };

        // Apply gravity
        vec2f32 rel = 1.f - ((surface->toplevel.gravity + 1.f) * .5f);
        frame.origin -= rel * (extent - anchor.extent);

        scene_window_set_frame(surface->toplevel.window.get(), frame);
        scene_transform_update(surface->scene.transform.get(), -surface->current.xdg.geometry.origin, 1);

        surface->toplevel.pending = false;
        if (surface->toplevel.queued) {
            configure_toplevel(surface, anchor.extent);
            way_xdg_surface_configure(surface);
            surface->toplevel.queued = false;
            surface->toplevel.pending = true;
        }
    } else {
        log_info("toplevel surface committed but not mapped, sending configure");
        send_premap_configure(surface);
    }
}

// -----------------------------------------------------------------------------

WAY_INTERFACE(xdg_toplevel) = {
    WAY_STUB(destroy),
    WAY_STUB(set_parent),
    .set_title  = WAY_ADDON_SIMPLE_STATE_REQUEST(way_toplevel, toplevel.title,  title,  title,  const char* title),
    .set_app_id = WAY_ADDON_SIMPLE_STATE_REQUEST(way_toplevel, toplevel.app_id, app_id, app_id, const char* app_id),
    WAY_STUB(show_window_menu),
    WAY_STUB(move),
    WAY_STUB(resize),
    .set_max_size = WAY_ADDON_SIMPLE_STATE_REQUEST(way_toplevel, toplevel.max_size, max_size, vec2i32(w, h), i32 w, i32 h),
    .set_min_size = WAY_ADDON_SIMPLE_STATE_REQUEST(way_toplevel, toplevel.min_size, min_size, vec2i32(w, h), i32 w, i32 h),
    WAY_STUB(set_maximized),
    WAY_STUB(unset_maximized),
    WAY_STUB(set_fullscreen),
    WAY_STUB(unset_fullscreen),
    WAY_STUB(set_minimized),
};
