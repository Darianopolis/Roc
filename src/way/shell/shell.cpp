#include "shell.hpp"

#include "../buffer/buffer.hpp"
#include "../surface/surface.hpp"
#include "../client.hpp"

static
void get_xdg_surface(wl_client* client, wl_resource* resource, u32 id, wl_resource* wl_surface)
{
    auto* surface = way_get_userdata<WaySurface>(wl_surface);
    surface->xdg_surface = way_resource_create_refcounted(xdg_surface, client, resource, id, surface);
}

WAY_INTERFACE(xdg_wm_base) = {
    .destroy = way_simple_destroy,
    .create_positioner = way_create_positioner,
    .get_xdg_surface = get_xdg_surface,
    WAY_STUB(pong),
};

WAY_BIND_GLOBAL(xdg_wm_base, bind)
{
    way_resource_create_unsafe(xdg_wm_base, bind.client, bind.version, bind.id, way_get_userdata<WayServer>(bind.data));
}

// -----------------------------------------------------------------------------

static
void get_toplevel(wl_client* client, wl_resource* resource, u32 id)
{
    auto* surface = way_get_userdata<WaySurface>(resource);
    surface->role = WaySurfaceRole::xdg_toplevel;
    surface->toplevel.resource = way_resource_create_refcounted(xdg_toplevel, client, resource, id, surface);

    surface->toplevel.window = scene_window_create(surface->client->scene.get());

    surface->scene.input_region->window = surface->toplevel.window.get();

    scene_tree_place_above(scene_window_get_tree(surface->toplevel.window.get()), nullptr, surface->scene.tree.get());
}

static
void ack_configure(wl_client* client, wl_resource* resource, u32 _serial)
{
    auto* surface = way_get_userdata<WaySurface>(resource);
    auto* server = surface->client->server;

    auto serial = WaySerial(_serial);

    if (serial > surface->sent_serial) {
        way_post_error(server, surface->xdg_surface, XDG_SURFACE_ERROR_INVALID_SERIAL,
            "Client acked configure {} which is higher than latest sent configure serial {}",
            serial.value, surface->sent_serial.value);
        return;
    }

    if (serial <= surface->acked_serial) {
        log_warn("Client acked old configure serial");
        return;
    }

    surface->queue.pending->xdg.acked_serial = serial;
    surface->queue.pending->set(WaySurfaceStateComponent::acked_serial);

    surface->acked_serial = serial;
}

WAY_INTERFACE(xdg_surface) = {
    .destroy = way_role_destroy,
    .get_toplevel = get_toplevel,
    .get_popup = way_get_popup,
    .set_window_geometry = WAY_ADDON_SIMPLE_STATE_REQUEST(way_xdg_surface, xdg.geometry, geometry, rect2i32({x, y}, {w, h}, xywh), i32 x, i32 y, i32 w, i32 h),
    .ack_configure = ack_configure,
};

void way_xdg_surface_apply(WaySurface* surface, WaySurfaceState& from)
{
    WAY_ADDON_SIMPLE_STATE_APPLY(from, surface->current, xdg.geometry,     geometry);
    WAY_ADDON_SIMPLE_STATE_APPLY(from, surface->current, xdg.acked_serial, acked_serial);

    if (!surface->current.is_set(WaySurfaceStateComponent::geometry) && surface->mapped) {
        surface->current.xdg.geometry = { {}, surface->current.buffer->extent, xywh };
    }
}

void way_xdg_surface_configure(WaySurface* surface)
{
    auto* server = surface->client->server;
    surface->sent_serial = way_next_serial(server);
    way_send(server, xdg_surface_send_configure, surface->xdg_surface, surface->sent_serial.value);
}

// -----------------------------------------------------------------------------

void way_toplevel_on_map_change(WaySurface* surface, bool mapped)
{
    if (mapped) {
        scene_window_map(surface->toplevel.window.get());
        for (auto* seat : scene_get_seats(surface->client->server->scene)) {
            scene_keyboard_focus(scene_seat_get_keyboard(seat), surface->scene.input_region.get());
        }
    } else {
        scene_window_unmap(surface->toplevel.window.get());
    }
}

static
void configure_toplevel(WaySurface* surface, vec2u32 extent)
{
    way_send(surface->client->server, xdg_toplevel_send_configure, surface->toplevel.resource,
        extent.x, extent.y,
        ptr_to(way_to_wl_array<const xdg_toplevel_state>({
            XDG_TOPLEVEL_STATE_ACTIVATED,
        }))
    );
}

static
void reposition(WaySurface* surface)
{
    vec2f32 extent = surface->current.xdg.geometry.extent;
    rect2f32 anchor = surface->toplevel.anchor;

    rect2f32 frame { anchor.origin, extent, xywh };

    // Apply gravity
    vec2f32 rel = 1.f - ((surface->toplevel.gravity + 1.f) * .5f);
    frame.origin -= rel * (extent - anchor.extent);

    scene_window_set_frame(surface->toplevel.window.get(), frame);
    scene_tree_set_translation(surface->scene.tree.get(), -surface->current.xdg.geometry.origin);
}

void way_toplevel_on_reposition(WaySurface* surface, rect2f32 frame, vec2f32 gravity)
{
    bool resize = surface->toplevel.anchor.extent != frame.extent;
    surface->toplevel.anchor = frame;
    surface->toplevel.gravity = gravity;
    if (resize) {
        if (surface->toplevel.pending > surface->acked_serial) {
            surface->toplevel.queued = true;
        } else {
            configure_toplevel(surface, frame.extent);
            way_xdg_surface_configure(surface);
            surface->toplevel.pending = surface->sent_serial;
        }
    } else {
        reposition(surface);
    }
}

void way_toplevel_on_close(WaySurface* surface)
{
    way_send(surface->client->server, xdg_toplevel_send_close, surface->toplevel.resource);
}

static
void send_premap_configure(WaySurface* surface)
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

void way_toplevel_apply(WaySurface* surface, WaySurfaceState& from)
{
    WAY_ADDON_SIMPLE_STATE_APPLY(from, surface->current, toplevel.title, title);
    WAY_ADDON_SIMPLE_STATE_APPLY(from, surface->current, toplevel.app_id, app_id);
    WAY_ADDON_SIMPLE_STATE_APPLY(from, surface->current, toplevel.max_size, max_size);
    WAY_ADDON_SIMPLE_STATE_APPLY(from, surface->current, toplevel.min_size, min_size);

    if (surface->mapped) {
        reposition(surface);

        if (surface->toplevel.queued) {
            configure_toplevel(surface, surface->toplevel.anchor.extent);
            way_xdg_surface_configure(surface);
            surface->toplevel.pending = surface->sent_serial;
            surface->toplevel.queued = false;
        }
    } else {
        log_info("toplevel surface committed but not mapped, sending configure");
        send_premap_configure(surface);
    }
}

// -----------------------------------------------------------------------------

WAY_INTERFACE(xdg_toplevel) = {
    .destroy = way_role_destroy,
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
