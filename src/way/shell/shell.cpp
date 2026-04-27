#include "shell.hpp"

#include "../buffer/buffer.hpp"
#include "../surface/surface.hpp"
#include "../client.hpp"

static
void get_xdg_surface(wl_client* client, wl_resource* resource, u32 id, wl_resource* wl_surface)
{
    auto* surface = way_get_userdata<WaySurface>(wl_surface);
    auto xdg_surface = ref_create<WayXdgSurface>();
    xdg_surface->resource = way_resource_create_refcounted(xdg_surface, client, resource, id, xdg_surface.get());
    surface->xdg = xdg_surface.get();
    way_surface_addon_register(surface, xdg_surface.get());
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
auto find_surface(WayClient* client, WmWindow* window) -> WaySurface*
{
    for (auto* surface : client->surfaces) {
        if (surface->toplevel && surface->toplevel->window.get() == window) {
            return surface;
        }
    }
    return nullptr;
}

void way_handle_window_event(WayClient* client, WmWindowEvent* event)
{
    switch (event->type) {
        break;case WmEventType::window_reposition_requested:
            way_toplevel_on_reposition(find_surface(client, event->window), event->reposition.frame, event->reposition.gravity);
        break;case WmEventType::window_close_requested:
            way_toplevel_on_close(find_surface(client, event->window));

        break;default:
            ;
    }
}

static
void get_toplevel(wl_client* client, wl_resource* resource, u32 id)
{
    auto* surface = way_get_userdata<WayXdgSurface>(resource)->surface;
    surface->role = WaySurfaceRole::xdg_toplevel;

    auto toplevel = ref_create<WayToplevel>();
    surface->toplevel = toplevel.get();
    way_surface_addon_register(surface, toplevel.get());

    toplevel->resource = way_resource_create_refcounted(xdg_toplevel, client, resource, id, toplevel.get());

    toplevel->window = wm_window_create(surface->client->wm.get());
    wm_window_set_focus(toplevel->window.get(), surface->scene.focus.get());

    scene_tree_place_above(wm_window_get_tree(toplevel->window.get()), nullptr, surface->scene.tree.get());
}

static
void ack_configure(wl_client* client, wl_resource* resource, u32 _serial)
{
    auto* xdg = way_get_userdata<WayXdgSurface>(resource);

    auto serial = WaySerial(_serial);

    if (serial > xdg->sent_serial) {
        way_post_error(xdg->resource, XDG_SURFACE_ERROR_INVALID_SERIAL,
            "Client acked configure {} which is higher than latest sent configure serial {}",
            serial.value, xdg->sent_serial.value);
        return;
    }

    if (serial <= xdg->acked_serial) {
        log_warn("Client acked old configure serial");
        return;
    }

    xdg->queue.pending->acked_serial = serial;
    xdg->queue.pending->set |= WayXdgSurfaceStateComponent::acked_serial;

    xdg->acked_serial = serial;
}

WAY_INTERFACE(xdg_surface) = {
    .destroy = way_simple_destroy,
    .get_toplevel = get_toplevel,
    .get_popup = way_get_popup,
    .set_window_geometry = [](wl_client* client, wl_resource* resource, i32 x, i32 y, i32 w, i32 h) {
        auto* xdg = way_get_userdata<WayXdgSurface>(resource);
        xdg->queue.pending->geometry = rect2i32({x, y}, {w, h}, xywh);
        xdg->queue.pending->set |= WayXdgSurfaceStateComponent::geometry;
    },
    .ack_configure = ack_configure,
};

WayXdgSurface::~WayXdgSurface()
{
    if (surface) {
        surface->xdg = nullptr;
    }
}

void WayXdgSurface::commit(WayCommitId id)
{
    queue.commit(id);
}

void WayXdgSurface::apply(WayCommitId id)
{
    if (auto from = queue.dequeue(id)) {
        if (from->set.contains(WayXdgSurfaceStateComponent::geometry)) {
            current.set |= WayXdgSurfaceStateComponent::geometry;
            current.geometry = from->geometry;
        }

        if (from->set.contains(WayXdgSurfaceStateComponent::acked_serial)) {
            current.set |= WayXdgSurfaceStateComponent::acked_serial;
            current.acked_serial = from->acked_serial;
        }
    }

    if (!current.set.contains(WayXdgSurfaceStateComponent::geometry) && surface->mapped) {
        current.geometry = { {}, surface->current.buffer->extent, xywh };
    }
}

void way_xdg_surface_configure(WaySurface* surface)
{
    auto* server = surface->client->server;
    surface->xdg->sent_serial = way_next_serial(server);
    way_send(xdg_surface, configure, surface->xdg->resource, surface->xdg->sent_serial.value);
}

// -----------------------------------------------------------------------------

void way_toplevel_on_map_change(WaySurface* surface, bool mapped)
{
    auto* toplevel = surface->toplevel;

    if (mapped) {
        wm_window_map(toplevel->window.get());
        for (auto* seat : wm_get_seats(surface->client->server->wm)) {
            seat_keyboard_focus(seat_get_keyboard(seat), surface->scene.focus.get());
        }
    } else {
        wm_window_unmap(toplevel->window.get());
    }
}

static
void configure_toplevel(WayToplevel* toplevel, vec2u32 extent)
{
    way_send(xdg_toplevel, configure, toplevel->resource,
        extent.x, extent.y,
        ptr_to(way_from_span<const xdg_toplevel_state>({
            XDG_TOPLEVEL_STATE_ACTIVATED,
        }))
    );
}

static
void reposition(WaySurface* surface)
{
    auto* xdg = surface->xdg;
    auto* toplevel = surface->toplevel;

    vec2f32 extent = xdg->current.geometry.extent;
    rect2f32 anchor = toplevel->anchor;

    rect2f32 frame { anchor.origin, extent, xywh };

    // Apply gravity
    vec2f32 rel = 1.f - ((toplevel->gravity + 1.f) * .5f);
    frame.origin -= rel * (extent - anchor.extent);

    wm_window_set_frame(toplevel->window.get(), frame);
    scene_tree_set_translation(surface->scene.tree.get(), -xdg->current.geometry.origin);
}

void way_toplevel_on_reposition(WaySurface* surface, rect2f32 frame, vec2f32 gravity)
{
    auto* toplevel = surface->toplevel;

    bool resize = toplevel->anchor.extent != frame.extent;
    toplevel->anchor = frame;
    toplevel->gravity = gravity;
    if (resize) {
        if (toplevel->pending > surface->xdg->acked_serial) {
            toplevel->queued = true;
        } else {
            configure_toplevel(toplevel, frame.extent);
            way_xdg_surface_configure(surface);
            toplevel->pending = surface->xdg->sent_serial;
        }
    } else {
        reposition(surface);
    }
}

void way_toplevel_on_close(WaySurface* surface)
{
    way_send(xdg_toplevel, close, surface->toplevel->resource);
}

static
void send_premap_configure(WayToplevel* toplevel)
{
    if (wl_resource_get_version(toplevel->resource) >= XDG_TOPLEVEL_WM_CAPABILITIES_SINCE_VERSION) {
        way_send(xdg_toplevel, wm_capabilities, toplevel->resource, ptr_to(way_from_span<const xdg_toplevel_wm_capabilities>({
            XDG_TOPLEVEL_WM_CAPABILITIES_FULLSCREEN,
        })));
    }

    configure_toplevel(toplevel, {0, 0});
    way_xdg_surface_configure(toplevel->surface);
}

void WayToplevel::commit(WayCommitId id)
{
    queue.commit(id);
}

void WayToplevel::apply(WayCommitId id)
{
    auto from = queue.dequeue(id);
    if (!from) return;

    if (from->set.contains(WayToplevelStateComponent::title)) {
        current.set |= WayToplevelStateComponent::title;
        current.title = std::move(from->title);
    }

    if (from->set.contains(WayToplevelStateComponent::app_id)) {
        current.set |= WayToplevelStateComponent::app_id;
        current.app_id = std::move(from->app_id);
    }

    if (from->set.contains(WayToplevelStateComponent::max_size)) {
        if (from->max_size == vec2i32(0, 0)) {
            current.set -= WayToplevelStateComponent::max_size;
        } else {
            current.set |= WayToplevelStateComponent::max_size;
            current.max_size = from->max_size;
        }
    }

    if (from->set.contains(WayToplevelStateComponent::min_size)) {
        if (from->min_size == vec2i32(0, 0)) {
            current.set -= WayToplevelStateComponent::min_size;
        } else {
            current.set |= WayToplevelStateComponent::min_size;
            current.min_size = from->min_size;
        }
    }

    if (surface->mapped) {
        reposition(surface);

        if (queued) {
            configure_toplevel(this, anchor.extent);
            way_xdg_surface_configure(surface);
            pending = surface->xdg->sent_serial;
            queued = false;
        }
    } else {
        log_info("toplevel surface committed but not mapped, sending configure");
        send_premap_configure(this);
    }
}

// -----------------------------------------------------------------------------

WAY_INTERFACE(xdg_toplevel) = {
    .destroy = way_simple_destroy,
    WAY_STUB(set_parent),
    .set_title  = [](wl_client* client, wl_resource* resource, const char* title) {
        auto* pending = way_get_userdata<WayToplevel>(resource)->queue.pending.get();
        pending->title = title;
        pending->set |= WayToplevelStateComponent::title;
    },
    .set_app_id = [](wl_client* client, wl_resource* resource, const char* app_id) {
        auto* pending = way_get_userdata<WayToplevel>(resource)->queue.pending.get();
        pending->app_id = app_id;
        pending->set |= WayToplevelStateComponent::app_id;
    },
    WAY_STUB(show_window_menu),
    WAY_STUB(move),
    WAY_STUB(resize),
    .set_max_size = [](wl_client* client, wl_resource* resource, i32 w, i32 h) {
        auto* pending = way_get_userdata<WayToplevel>(resource)->queue.pending.get();
        pending->max_size = vec2i32(w, h);
        pending->set |= WayToplevelStateComponent::max_size;
    },
    .set_min_size = [](wl_client* client, wl_resource* resource, i32 w, i32 h) {
        auto* pending = way_get_userdata<WayToplevel>(resource)->queue.pending.get();
        pending->min_size = vec2i32(w, h);
        pending->set |= WayToplevelStateComponent ::min_size;
    },
    WAY_STUB(set_maximized),
    WAY_STUB(unset_maximized),
    WAY_STUB(set_fullscreen),
    WAY_STUB(unset_fullscreen),
    WAY_STUB(set_minimized),
};

WayToplevel::~WayToplevel()
{
    if (surface) {
        surface->toplevel = nullptr;
        surface->role = WaySurfaceRole::none;
    }
}
