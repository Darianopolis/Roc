#include "surface.hpp"

#include "../shell/shell.hpp"
#include "../buffer/buffer.hpp"
#include "../client.hpp"

WAY_INTERFACE(wl_region) = {
    .destroy = way_simple_destroy,
    .add = [](wl_client* client, wl_resource* resource, i32 x, i32 y, i32 w, i32 h) {
        way_get_userdata<WayRegion>(resource)->region.add({{x, y}, {w, h}, xywh});
    },
    .subtract = [](wl_client* client, wl_resource* resource, i32 x, i32 y, i32 w, i32 h) {
        way_get_userdata<WayRegion>(resource)->region.subtract({{x, y}, {w, h}, xywh});
    }
};

static
void create_region(wl_client* client, wl_resource* resource, u32 id)
{
    auto region = ref_create<WayRegion>();
    region->resource = way_resource_create_refcounted(wl_region, client, resource, id, region.get());
}

// -----------------------------------------------------------------------------

static
void create_surface(wl_client* client, wl_resource* resource, u32 id)
{
    auto surface = ref_create<WaySurface>();

    surface->client = way_client_from(way_get_userdata<WayServer>(resource), client);
    surface->client->surfaces.emplace_back(surface.get());

    auto* server = surface->client->server;

    surface->scene.tree = scene_tree_create();
    surface->scene.tree->userdata = {server->userdata_id, surface.get()};
    scene_tree_set_enabled(surface->scene.tree.get(), false);

    surface->scene.texture = scene_texture_create();
    scene_tree_place_above(surface->scene.tree.get(), nullptr, surface->scene.texture.get());

    surface->scene.input_region = scene_input_region_create(surface->client->scene.get());
    scene_tree_place_above(surface->scene.tree.get(), nullptr, surface->scene.input_region.get());

    surface->wl_surface = way_resource_create_refcounted(wl_surface, client, resource, id, surface.get());
}

WAY_INTERFACE(wl_compositor) = {
    .create_surface = create_surface,
    .create_region  = create_region,
};

WAY_BIND_GLOBAL(wl_compositor, bind)
{
    log_error("COMPOSITOR 1: {}", (void*)static_cast<WayObject*>(way_get_userdata<WayServer>(bind.data)));
    way_resource_create_unsafe(wl_compositor, bind.client, bind.version, bind.id, way_get_userdata<WayServer>(bind.data));
}

// -----------------------------------------------------------------------------

static
void offset(wl_client* client, wl_resource* resource, i32 dx, i32 dy)
{
    auto* surface = way_get_userdata<WaySurface>(resource);
    surface->queue.pending->surface.offset += vec2i32{dx, dy};
}

static
void attach(wl_client* client, wl_resource* resource, wl_resource* wl_buffer, i32 dx, i32 dy)
{
    auto* surface = way_get_userdata<WaySurface>(resource);
    auto* pending = surface->queue.pending.get();

    debug_assert(!pending->image);

    if (!wl_buffer) {
        pending->buffer = nullptr;
        pending->unset(WaySurfaceStateComponent::buffer);
        return;
    }

    pending->buffer = way_get_userdata<WayBuffer>(wl_buffer);
    pending->set(WaySurfaceStateComponent::buffer);

    pending->surface.offset += vec2i32{dx, dy};
}

static
void damage(wl_client* client, wl_resource* resource, i32 x, i32 y, i32 width, i32 height)
{
    auto* surface = way_get_userdata<WaySurface>(resource);

    surface->queue.pending->surface.damage.damage({{x, y}, {width, height}, xywh});
}

static
void damage_buffer(wl_client* client, wl_resource* resource, i32 x, i32 y, i32 width, i32 height)
{
    auto* surface = way_get_userdata<WaySurface>(resource);

    surface->queue.pending->buffer_damage.damage({{x, y}, {width, height}, xywh});
}

static
void frame(wl_client* client, wl_resource* resource, u32 id)
{
    auto* surface = way_get_userdata<WaySurface>(resource);

    auto callback = way_resource_create(client, &wl_callback_interface, resource, id, nullptr, nullptr, false);

    surface->queue.pending->surface.frame_callbacks.emplace_back(callback);
}

void way_surface_on_redraw(WaySurface* surface)
{
    auto* server = surface->client->server;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(way_get_elapsed(server)).count();

    auto send_frame_callbacks = [&](WayResourceList& list) {
        while (auto callback = list.front()) {
            way_send(server, wl_callback_send_done, callback, ms);
            wl_resource_destroy(callback);
        }
    };

    send_frame_callbacks(surface->current.surface.frame_callbacks);
    for (auto& pending : surface->queue.cached) {
        if (!pending->commit) continue;
        send_frame_callbacks(pending->surface.frame_callbacks);
    }

    way_queue_client_flush(server);
}

static
void set_input_region(wl_client* client, wl_resource* resource, wl_resource* region)
{
    auto* surface = way_get_userdata<WaySurface>(resource);
    auto* pending = surface->queue.pending.get();

    if (region) {
        pending->surface.input_region = way_get_userdata<WayRegion>(region)->region;
        pending->set(WaySurfaceStateComponent::input_region);
    } else {
        pending->unset(WaySurfaceStateComponent::input_region);
    }
}

WaySurfaceState::~WaySurfaceState()
{
    // TODO: Empty callbacks
}

static
void surface_set_mapped(WaySurface* surface, bool mapped)
{
    if (mapped == surface->mapped) return;
    surface->mapped = mapped;

    log_info("Surface {} was {}", (void*)surface, mapped ? "mapped" : "unmapped");

    scene_tree_set_enabled(surface->scene.tree.get(), mapped);

    if (surface->role == WaySurfaceRole::xdg_toplevel) {
        way_toplevel_on_map_change(surface, mapped);
    }
}

static
void update_map_state(WaySurface* surface)
{
    bool can_be_mapped =
           surface->current.buffer
        && surface->current.image
        && surface->wl_surface;

    surface_set_mapped(surface, can_be_mapped);
}

static
void apply(WaySurface* surface, WaySurfaceState& from)
{
    auto& to = surface->current;

    to.commit = from.commit;

    to.committed.set |=  from.committed.set;
    to.committed.set &= ~from.committed.unset;

    WAY_ADDON_SIMPLE_STATE_APPLY(from, surface->current, buffer_transform, buffer_transform);
    WAY_ADDON_SIMPLE_STATE_APPLY(from, surface->current, buffer_scale,     buffer_scale);

    to.surface.frame_callbacks.take_and_append_all(std::move(from.surface.frame_callbacks));

    // Offset

    if (from.surface.offset.x || from.surface.offset.y) {
        switch (surface->role) {
            break;case WaySurfaceRole::cursor:
                  case WaySurfaceRole::drag_icon:
                scene_tree_set_translation(surface->scene.tree.get(), surface->scene.tree->translation + vec2f32(from.surface.offset));
            break;default:
                ;
        }
    }

    // Buffer state

    if (from.is_set(WaySurfaceStateComponent::buffer)) {
        to.buffer = std::move(from.buffer);
        to.image  = std::move(from.image);

        scene_texture_set_image(surface->scene.texture.get(),
            to.image.get(),
            surface->client->server->sampler.get(),
            GpuBlendMode::premultiplied);

        if (from.buffer_damage) {
            scene_texture_damage(surface->scene.texture.get(), from.buffer_damage.bounds());
        }

    } else if (from.is_unset(WaySurfaceStateComponent::buffer)) {
        to.buffer = nullptr;

        scene_texture_set_image(surface->scene.texture.get(), nullptr, nullptr, GpuBlendMode::none);
    }

    // Buffer source / destination

    way_viewport_apply(surface, from);

    // Input regions

    if (from.is_set(WaySurfaceStateComponent::input_region)) {
        // TODO: Clip set input_regions against surface bounds?
        scene_input_region_set_region(surface->scene.input_region.get(), std::move(from.surface.input_region));

    } else if (!to.is_set(WaySurfaceStateComponent::input_region) && to.buffer) {
        // Unset input_region fills entire surface
        scene_input_region_set_region(surface->scene.input_region.get(),
            {{{}, to.buffer->extent, xywh}});
    }

    // Map state

    update_map_state(surface);

    // Component state

    if (surface->xdg_surface) {
        way_xdg_surface_apply(surface, from);
    }

    switch (surface->role) {
        break;case WaySurfaceRole::xdg_toplevel:
            way_toplevel_apply(surface, from);
        break;case WaySurfaceRole::subsurface:
        break;case WaySurfaceRole::cursor:
        break;case WaySurfaceRole::drag_icon:
        break;case WaySurfaceRole::xdg_popup:
            way_popup_apply(surface, from);
        break;case WaySurfaceRole::none:
            ;
    }

    way_subsurface_apply(surface, from);
}

static
auto is_blocked_by_parent(WaySurface* surface, WaySurfaceState& pending) -> bool
{
    if (!pending.is_set(WaySurfaceStateComponent::parent_commit)) return false;

    if (surface->parent && surface->parent->current.commit < pending.parent.commit) {
        return true;
    } else if (!surface->parent) {
        log_warn("parent_commit set but parent is gone, applying");
    }

    return false;
}

static
void flush(WaySurface* surface)
{
    // TODO: Queued applications

    auto prev_applied_commit_id = surface->current.commit;

    while (!surface->queue.cached.empty()) {
        auto& packet = *surface->queue.cached.front().get();

        if (is_blocked_by_parent(surface, packet)) break;

        // Convert surface damage to buffer damage

        if (packet.surface.damage) {
            auto bounds = packet.surface.damage.bounds();

            // Apply buffer transform
            auto transform = packet.is_set(WaySurfaceStateComponent::buffer_transform)
                ? packet.buffer_transform
                : surface->current.buffer_transform;
            debug_assert(transform == WL_OUTPUT_TRANSFORM_NORMAL, "TODO: Support buffer transforms");

            // Apply buffer scale
            bounds.min *= packet.buffer_scale;
            bounds.max *= packet.buffer_scale;

            packet.buffer_damage.damage(bounds);
            packet.surface.damage.clear();
        }

        // Check for buffer ready

        debug_assert(!packet.image);
        if (packet.buffer && !(packet.image = packet.buffer->acquire(surface, packet.buffer_damage))) {
            debug_kill();
        }

        apply(surface, packet);

        surface->queue.cached.pop_front();
    }

    if (surface->current.commit == prev_applied_commit_id) return;

    // Flush subsurface state recursively

    auto* server = surface->client->server;

    for (auto* child : surface->scene.tree->children) {
        auto* tree = dynamic_cast<SceneTree*>(child);
        if (!tree) continue;
        if (tree->userdata.id != server->userdata_id) continue;
        flush(way_get_userdata<WaySurface>(tree->userdata.data));
    }
}

static
void commit(wl_client* client, wl_resource* resource)
{
    auto* surface = way_get_userdata<WaySurface>(resource);

    auto pending = surface->queue.pending;
    pending->commit = ++surface->last_commit_id;
    surface->queue.cached.emplace_back(pending);
    surface->queue.pending = ref_create<WaySurfaceState>();

    // Queue frame request for frame callbacks

    if (pending->surface.frame_callbacks.front()) {
        wm_request_frame(surface->client->server->wm);
    }

    // Apply subsurface synchronization barriers

    if (surface->role == WaySurfaceRole::subsurface) {
        way_subsurface_commit(surface, *pending.get());
    }

    if (!pending->is_set(WaySurfaceStateComponent::buffer)) {
        debug_assert(!pending->buffer_damage,  "TODO: wl_surface::damage_buffer without attached buffer");
        debug_assert(!pending->surface.damage, "TODO: wl_surface::damage without attached buffer");
    }

    // Attempt to flush any state immediately

    flush(surface);
}

WAY_INTERFACE(wl_surface) = {
    .destroy = way_simple_destroy,
    .attach = attach,
    .damage = damage,
    .frame = frame,
    WAY_STUB_QUIET(set_opaque_region),
    .set_input_region = set_input_region,
    .commit = commit,
    .set_buffer_transform = WAY_ADDON_SIMPLE_STATE_REQUEST(WaySurface, buffer_transform, buffer_transform, wl_output_transform(bt), i32 bt),
    .set_buffer_scale     = WAY_ADDON_SIMPLE_STATE_REQUEST(WaySurface, buffer_scale,     buffer_scale,     scale,                   i32 scale),
    .damage_buffer = damage_buffer,
    .offset = offset,
};

// -----------------------------------------------------------------------------

WaySurface::~WaySurface()
{
    scene.tree->userdata = {};
    debug_assert(std::erase(client->surfaces, this));
}

void way_role_destroy(wl_client* client, wl_resource* resource)
{
    auto* surface = way_get_userdata<WaySurface>(resource);
    surface->role = WaySurfaceRole::none;
    way_simple_destroy(client, resource);
}
