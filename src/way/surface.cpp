#include "internal.hpp"

WAY_INTERFACE(wl_region) = {
    .destroy = way_simple_destroy,
    .add = [](wl_client* client, wl_resource* resource, i32 x, i32 y, i32 w, i32 h) {
        way_get_userdata<way_region>(resource)->region.add({{x, y}, {w, h}, core_xywh});
    },
    .subtract = [](wl_client* client, wl_resource* resource, i32 x, i32 y, i32 w, i32 h) {
        way_get_userdata<way_region>(resource)->region.subtract({{x, y}, {w, h}, core_xywh});
    }
};

static
void create_region(wl_client* client, wl_resource* resource, u32 id)
{
    auto region = core_create<way_region>();
    region->resource = way_resource_create_refcounted(wl_region, client, resource, id, region.get());
}

// -----------------------------------------------------------------------------

static
void create_surface(wl_client* client, wl_resource* resource, u32 id)
{
    auto surface = core_create<way_surface>();

    surface->client = way_client_from(way_get_userdata<way_server>(resource), client);
    surface->client->surfaces.emplace_back(surface.get());

    surface->pending = &surface->cached.emplace_back();

    auto* scene = surface->client->server->scene;

    surface->scene.tree = scene_tree_create(scene);
    surface->scene.tree->userdata = surface.get();
    scene_tree_set_enabled(surface->scene.tree.get(), false);

    surface->scene.transform = scene_transform_create(scene);

    surface->scene.texture = scene_texture_create(scene);
    scene_node_set_transform(surface->scene.texture.get(), surface->scene.transform.get());
    scene_tree_place_above(surface->scene.tree.get(), nullptr, surface->scene.texture.get());

    surface->scene.input_region = scene_input_region_create(surface->client->scene.get());
    scene_node_set_transform(surface->scene.input_region.get(), surface->scene.transform.get());
    scene_tree_place_above(surface->scene.tree.get(), nullptr, surface->scene.input_region.get());

    surface->wl_surface = way_resource_create_refcounted(wl_surface, client, resource, id, surface.get());
}

WAY_INTERFACE(wl_compositor) = {
    .create_surface = create_surface,
    .create_region  = create_region,
};

WAY_BIND_GLOBAL(wl_compositor)
{
    way_resource_create_unsafe(wl_compositor, client, version, id, way_get_userdata<way_server>(data));
}

// -----------------------------------------------------------------------------

static
void attach(wl_client* client, wl_resource* resource, wl_resource* wl_buffer, i32 x, i32 y)
{
    auto* surface = way_get_userdata<way_surface>(resource);
    auto* pending = surface->pending;

    pending->buffer.lock = nullptr;

    if (!wl_buffer) {
        pending->buffer.handle = nullptr;
        pending->unset(way_surface_committed_state::buffer);
        return;
    }

    pending->buffer.handle = way_get_userdata<way_buffer>(wl_buffer);
    pending->set(way_surface_committed_state::buffer);

    if (x || y) {
        if (wl_resource_get_version(resource) >= WL_SURFACE_OFFSET_SINCE_VERSION) {
            way_post_error(surface->client->server, resource, WL_SURFACE_ERROR_INVALID_OFFSET,
                "Non-zero offset not allowed in wl_surface::attach since version {}", WL_SURFACE_OFFSET_SINCE_VERSION);
        } else {
            pending->surface.delta = { x, y };
            pending->set(way_surface_committed_state::offset);
        }
    }
}

static
void frame(wl_client* client, wl_resource* resource, u32 id)
{
    auto* surface = way_get_userdata<way_surface>(resource);
    auto* pending = surface->pending;

    auto callback = way_resource_create_(client, &wl_callback_interface, resource, id, nullptr, nullptr, false);

    pending->surface.frame_callbacks.emplace_back(callback);
}

void way_surface_on_redraw(way_surface* surface)
{
    auto* server = surface->client->server;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(way_get_elapsed(server)).count();

    auto send_frame_callbacks = [&](way_resource_list& list) {
        while (auto callback = list.front()) {
            way_send(server, wl_callback_send_done, callback, ms);
            wl_resource_destroy(callback);
        }
    };

    send_frame_callbacks(surface->current.surface.frame_callbacks);
    for (auto& pending : surface->cached) {
        if (!pending.commit) continue;
        send_frame_callbacks(pending.surface.frame_callbacks);
    }

    way_queue_client_flush(server);
}

static
void set_input_region(wl_client* client, wl_resource* resource, wl_resource* region)
{
    auto* surface = way_get_userdata<way_surface>(resource);
    auto* pending = surface->pending;

    if (region) {
        pending->surface.input_region = way_get_userdata<way_region>(region)->region;
        pending->set(way_surface_committed_state::input_region);
    } else {
        pending->unset(way_surface_committed_state::input_region);
    }
}

way_surface_state::~way_surface_state()
{
    // TODO: Empty callbacks
}

static
void surface_set_mapped(way_surface* surface, bool mapped)
{
    if (mapped == surface->mapped) return;
    surface->mapped = mapped;

    log_info("Surface {} was {}", (void*)surface, mapped ? "mapped" : "unmapped");

    scene_tree_set_enabled(surface->scene.tree.get(), mapped);

    if (surface->role == way_surface_role::xdg_toplevel) {
        way_toplevel_on_map_change(surface, mapped);
    }
}

static
void update_map_state(way_surface* surface)
{
    bool can_be_mapped =
           surface->current.buffer.handle
        && surface->current.buffer.handle->image
        && surface->wl_surface;

    surface_set_mapped(surface, can_be_mapped);
}

static
void apply(way_surface* surface, way_surface_state& from)
{
    auto& to = surface->current;

    to.commit = from.commit;

    to.committed.set |=  from.committed.set;
    to.committed.set &= ~from.committed.unset;

    WAY_ADDON_SIMPLE_STATE_APPLY(from, surface->current, buffer.transform, buffer_transform);
    WAY_ADDON_SIMPLE_STATE_APPLY(from, surface->current, buffer.scale,     buffer_scale);

    to.surface.frame_callbacks.take_and_append_all(std::move(from.surface.frame_callbacks));

    // Buffer state

    if (from.is_set(way_surface_committed_state::buffer)) {
        to.buffer.handle = std::move(from.buffer.handle);
        to.buffer.lock   = std::move(from.buffer.lock);

        scene_texture_set_image(surface->scene.texture.get(),
            to.buffer.handle->image.get(),
            surface->client->server->sampler.get(),
            gpu_blend_mode::premultiplied);

    } else if (from.is_unset(way_surface_committed_state::buffer)) {
        to.buffer.handle = nullptr;
        to.buffer.lock   = nullptr;

        scene_texture_set_image(surface->scene.texture.get(), nullptr, nullptr, gpu_blend_mode::none);
    }

    // Buffer source / destination

    way_viewport_apply(surface, from);

    // Input regions

    if (from.is_set(way_surface_committed_state::input_region)) {
        // TODO: Clip set input_regions against surface bounds?
        scene_input_region_set_region(surface->scene.input_region.get(), std::move(from.surface.input_region));
    } else if (!to.is_set(way_surface_committed_state::input_region) && to.buffer.handle) {
        // Unset input_region fills entire surface
        scene_input_region_set_region(surface->scene.input_region.get(),
            {{{}, to.buffer.handle->extent, core_xywh}});
    }

    // Map state

    update_map_state(surface);

    // Component state

    if (surface->xdg_surface) {
        way_xdg_surface_apply(surface, from);
    }

    switch (surface->role) {
        break;case way_surface_role::xdg_toplevel:
            way_toplevel_apply(surface, from);
        break;case way_surface_role::subsurface:
        break;case way_surface_role::cursor:
        break;case way_surface_role::drag_icon:
        break;case way_surface_role::xdg_popup:
            way_popup_apply(surface, from);
        break;case way_surface_role::none:
            ;
    }

    way_subsurface_apply(surface, from);
}

static
auto is_blocked_by_parent(way_surface* surface, way_surface_state& pending) -> bool
{
    if (!pending.is_set(way_surface_committed_state::parent_commit)) return false;

    if (surface->parent && surface->parent->current.commit < pending.parent.commit) {
        return true;
    } else if (!surface->parent) {
        log_warn("parent_commit set but parent is gone, applying");
    }

    return false;
}

static
void flush(way_surface* surface)
{
    // TODO: Queued applications

    auto prev_applied_commit_id = surface->current.commit;

    while (surface->cached.size() > 1) {
        auto& packet = surface->cached.front();

        if (is_blocked_by_parent(surface, packet)) break;

        // Check for buffer ready
        if (packet.buffer.lock && !packet.buffer.lock->buffer->is_ready(surface)) {
            core_debugkill();
        }

        apply(surface, packet);

        surface->cached.pop_front();
    }

    if (surface->current.commit == prev_applied_commit_id) return;

    // Flush subsurface state recursively

    for (auto* child : surface->scene.tree->children) {
        if (auto* tree = dynamic_cast<scene_tree*>(child)) {
            flush(core_object_cast<way_surface>(tree->userdata));
        }
    }
}

static
void commit(wl_client* client, wl_resource* resource)
{
    auto* surface = way_get_userdata<way_surface>(resource);

    auto* pending = surface->pending;
    pending->commit = ++surface->last_commit_id;
    surface->pending = &surface->cached.emplace_back();

    // Queue frame request for frame callbacks

    if (pending->surface.frame_callbacks.front()) {
        scene_request_frame(surface->client->server->scene);
    }

    // Apply subsurface synchronization barriers

    if (surface->role == way_surface_role::subsurface) {
        way_subsurface_commit(surface, *pending);
    }

    // Begin acquisition process for buffers

    if (pending->is_set(way_surface_committed_state::buffer)) {
        if (pending->buffer.handle) {
            pending->buffer.lock = pending->buffer.handle->commit(surface);
        }
    }

    // Attempt to flush any state immediately

    flush(surface);
}

WAY_INTERFACE(wl_surface) = {
    .destroy = way_simple_destroy,
    .attach = attach,
    WAY_STUB_QUIET(damage),
    .frame = frame,
    WAY_STUB_QUIET(set_opaque_region),
    .set_input_region = set_input_region,
    .commit = commit,
    .set_buffer_transform = WAY_ADDON_SIMPLE_STATE_REQUEST(way_surface, buffer.transform, buffer_transform, wl_output_transform(bt), i32 bt),
    .set_buffer_scale     = WAY_ADDON_SIMPLE_STATE_REQUEST(way_surface, buffer.scale,     buffer_scale,     scale,                   i32 scale),
    WAY_STUB_QUIET(damage_buffer),
    WAY_STUB_QUIET(offset),
};

// -----------------------------------------------------------------------------

way_surface::~way_surface()
{
    scene_node_unparent(scene.tree.get());
    scene_node_set_transform(scene.transform.get(), nullptr);
    scene.tree->userdata = nullptr;
    std::erase(client->surfaces, this);
}
