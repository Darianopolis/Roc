#include "protocol.hpp"

#include "core/math.hpp"

const u32 wroc_wl_compositor_version = 6;
const u32 wroc_wl_subcompositor_version = 1;

static
void wroc_wl_compositor_create_region(wl_client* client, wl_resource* resource, u32 id)
{
    auto* new_resource = wroc_resource_create(client, &wl_region_interface, wl_resource_get_version(resource), id);
    auto* region = core::create_unsafe<wroc_region>();
    region->resource = new_resource;
    wroc_resource_set_implementation_refcounted(new_resource, &wroc_wl_region_impl, region);
}

static
void wroc_wl_compositor_create_surface(wl_client* client, wl_resource* resource, u32 id)
{
    auto* new_resource = wroc_resource_create(client, &wl_surface_interface, wl_resource_get_version(resource), id);
    auto* surface = core::create_unsafe<wroc_surface>();
    surface->resource = new_resource;
    server->surfaces.emplace_back(surface);

    // Add surface to its own surface stack
    surface->pending->surface_stack.emplace_back(surface);
    surface->pending->committed |= wroc_surface_committed_state::surface_stack;

    surface->current.input_region = {{{0, 0}, {INT32_MAX, INT32_MAX}, core::minmax}},
    surface->current.buffer_scale = 1.f;

    // Use default cursor
    surface->cursor = wroc_cursor_get_shape(server->cursor.get(), WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT);

    wroc_resource_set_implementation_refcounted(new_resource, &wroc_wl_surface_impl, surface);

    // Enter primary output unconditionally
    wroc_output_enter_surface(server->output_layout->primary.get(), surface);
}

const struct wl_compositor_interface wroc_wl_compositor_impl = {
    .create_surface = wroc_wl_compositor_create_surface,
    .create_region  = wroc_wl_compositor_create_region,
};

void wroc_wl_compositor_bind_global(wl_client* client, void* data, u32 version, u32 id)
{
    auto* new_resource = wroc_resource_create(client, &wl_compositor_interface, version, id);
    wroc_resource_set_implementation(new_resource, &wroc_wl_compositor_impl, nullptr);
};

// -----------------------------------------------------------------------------

static
void wroc_wl_region_add(wl_client* client, wl_resource* resource, i32 x, i32 y, i32 width, i32 height)
{
    auto* region = wroc_get_userdata<wroc_region>(resource);
    region->region.add({{x, y}, {width, height}, core::xywh});
}

static
void wroc_wl_region_subtract(wl_client* client, wl_resource* resource, i32 x, i32 y, i32 width, i32 height)
{
    auto* region = wroc_get_userdata<wroc_region>(resource);

    region->region.subtract({{x, y}, {width, height}, core::xywh});
}

const struct wl_region_interface wroc_wl_region_impl = {
    .destroy  = wroc_simple_resource_destroy_callback,
    .add      = wroc_wl_region_add,
    .subtract = wroc_wl_region_subtract,
};

// -----------------------------------------------------------------------------

static
void wroc_wl_surface_attach(wl_client* client, wl_resource* resource, wl_resource* wl_buffer, i32 x, i32 y)
{
    auto* surface = wroc_get_userdata<wroc_surface>(resource);

    surface->pending->buffer_lock = nullptr;
    surface->pending->buffer = wl_buffer ? wroc_get_userdata<wroc_buffer>(wl_buffer) : nullptr;
    surface->pending->committed |= wroc_surface_committed_state::buffer;

    if (x || y) {
        if (wl_resource_get_version(resource) >= WL_SURFACE_OFFSET_SINCE_VERSION) {
            wroc_post_error(resource, WL_SURFACE_ERROR_INVALID_OFFSET,
                "Non-zero offset not allowed in wl_surface::attach since version {}", WL_SURFACE_OFFSET_SINCE_VERSION);
        } else {
            surface->pending->delta = { x, y };
            surface->pending->committed |= wroc_surface_committed_state::offset;
        }
    }
}

static
void wroc_wl_surface_frame(wl_client* client, wl_resource* resource, u32 callback)
{
    auto* surface = wroc_get_userdata<wroc_surface>(resource);
    auto new_resource = wroc_resource_create(client, &wl_callback_interface, 1, callback);
    surface->pending->frame_callbacks.emplace_back(new_resource);
    wroc_resource_set_implementation(new_resource, nullptr, surface);
}

static
void wroc_wl_surface_set_opaque_region(wl_client* client, wl_resource* resource, wl_resource* opaque_region)
{
    auto* surface = wroc_get_userdata<wroc_surface>(resource);
    auto* region = wroc_get_userdata<wroc_region>(opaque_region);
    if (region) {
        surface->pending->opaque_region = region->region;
    } else {
        surface->pending->opaque_region.clear();
    }
    surface->pending->committed |= wroc_surface_committed_state::opaque_region;
}

static
void wroc_wl_surface_set_input_region(wl_client* client, wl_resource* resource, wl_resource* input_region)
{
    auto* surface = wroc_get_userdata<wroc_surface>(resource);
    auto* region = wroc_get_userdata<wroc_region>(input_region);
    if (region) {
        surface->pending->input_region = region->region;
    } else {
        surface->pending->input_region.clear();
        surface->pending->input_region.add({{0, 0}, {INT32_MAX, INT32_MAX}, core::minmax});
    }
    surface->pending->committed |= wroc_surface_committed_state::input_region;
}

static
void wroc_wl_surface_offset(wl_client* client, wl_resource* resource, i32 x, i32 y)
{
    auto* surface = wroc_get_userdata<wroc_surface>(resource);
    surface->pending->delta = { x, y };
    surface->pending->committed |= wroc_surface_committed_state::offset;
}

bool wroc_surface_is_focusable(wroc_surface* surface)
{
    return surface->mapped && surface->role == wroc_surface_role::xdg_toplevel;
}

static
void refocus_on_unmap()
{
    auto found = std::ranges::find_last_if(server->surfaces, wroc_surface_is_focusable);
    auto target = found.empty() ? nullptr : found.front();
    log_info("Refocusing -> {}", (void*)target);
    wroc_keyboard_enter(server->seat->keyboard.get(), target);
}

static
void surface_set_mapped(wroc_surface* surface, bool mapped)
{
    if (mapped == surface->mapped) return;
    surface->mapped = mapped;

    log_info("Surface {} was {}", (void*)surface, mapped ? "mapped" : "unmapped");

    for (auto& addon : surface->addons) {
        addon->on_mapped_change();
    }

    if (!mapped) {
        if (surface == server->seat->keyboard->focused_surface.get()) {
            refocus_on_unmap();
        }

        if (surface == server->seat->pointer->focused_surface.get()) {
            // TODO: Re-check surface under pointer
            server->seat->pointer->focused_surface = nullptr;
        }
    }
}

void wroc_surface_update_map_state(wroc_surface* surface)
{
    bool can_be_mapped =
           surface->current.buffer
        && surface->current.buffer->image
        && surface->role_addon
        && surface->resource;

    surface_set_mapped(surface, can_be_mapped);
}

static
void apply_state(wroc_surface* surface, wroc_surface_state& from)
{
    auto& to = surface->current;

    // Update frame callbacks

    to.frame_callbacks.take_and_append_all(std::move(from.frame_callbacks));

    // Update buffer

    if (from.committed.contains(wroc_surface_committed_state::buffer)) {
        if (from.buffer && from.buffer->resource) {
            to.buffer      = std::move(from.buffer);
            to.buffer_lock = std::move(from.buffer_lock);
        } else {
            if (from.buffer) {
                log_warn("Pending buffer was destroyed, surface contents will be cleared");
            }
            to.buffer = nullptr;
            to.buffer_lock = nullptr;
        }

        from.buffer = nullptr;
        from.buffer_lock = nullptr;
    }

    // Update opaque region

    if (from.committed.contains(wroc_surface_committed_state::opaque_region)) {
        to.opaque_region = std::move(from.opaque_region);
    }

    // Update input region

    if (from.committed.contains(wroc_surface_committed_state::input_region)) {
        to.input_region = std::move(from.input_region);
    }

    // Update offset delta

    if (from.committed.contains(wroc_surface_committed_state::offset)) {
        to.delta += from.delta;
    }

    // Update surface stack

    if (std::erase_if(from.surface_stack, [](auto& w) { return !w.surface; })) {
        // Clean out tombstones
        from.committed |= wroc_surface_committed_state::surface_stack;
    }

    if (from.committed.contains(wroc_surface_committed_state::surface_stack)) {
        to.surface_stack.clear();
        to.surface_stack.append_range(from.surface_stack);
    }

    // Update commit core::Flags

    to.committed |= from.committed;
    from.committed = {};
}

#define WROC_NOISY_COMMIT 0

static
void wroc_wl_surface_commit(wl_client* client, wl_resource* resource)
{
    auto* surface = wroc_get_userdata<wroc_surface>(resource);

    auto& packet = surface->cached.back();
    packet.id = ++surface->committed;

#if WROC_NOISY_COMMIT
    log_debug("New surface commit (id = {})", packet.id);
#endif

    for (auto& addon : surface->addons) {
        addon->commit(packet.id);
    }

    // Commit and begin acquisition process for buffers

    if (packet.state.committed.contains(wroc_surface_committed_state::buffer)) {
        if (packet.state.buffer) {
            packet.state.buffer_lock = packet.state.buffer->commit(surface);
        }
    }

    surface->pending = &surface->cached.emplace_back().state;

    // We need to make sure the surface stack remains persistently in pending
    // TODO: We really want to recycle surface state packets
    surface->pending->surface_stack.append_range(packet.state.surface_stack);

    wroc_surface_flush_apply(surface);
}

static
bool is_blocked_by_parent_commit(wroc_surface* surface, wroc_surface_state& state, wroc_commit_id id)
{
    if (!(state.committed.contains(wroc_surface_committed_state::parent_commit))) return false;

    auto* subsurface = wroc_surface_get_addon<wroc_subsurface>(surface);
    if (subsurface
            && subsurface->parent
            && subsurface->parent->applied < state.parent_commit) {
#if WROC_NOISY_SUBSURFACES
        log_warn("synchronized state {} cannot be dequeued, expected parent commit {}, got {}",
            id, state.parent_commit, subsurface->parent->applied);
#endif
        return true;
    } else {
        if (!subsurface || !subsurface->parent) {
            log_warn("Synchronized state {} is orphaned due to missing subsurface or parent, applying", id);
        }
#if WROC_NOISY_SUBSURFACES
        else {
            log_debug("Synchronized state {} can be dequeued, parent commit {} >= {}",
                id, subsurface->parent->applied, state.parent_commit);
        }
#endif
    }

    return false;
}

void wroc_surface_flush_apply(wroc_surface* surface)
{
    if (surface->apply_queued) return;

    auto prev_applied_commit_id = surface->applied;

    while (surface->cached.size() > 1) {
        auto& packet = surface->cached.front();

        if (is_blocked_by_parent_commit(surface, packet.state, packet.id)) break;

        // Check for buffer ready
        if (packet.state.buffer_lock && !packet.state.buffer_lock->buffer->is_ready(surface)) break;

        apply_state(surface, packet.state);

        surface->applied = packet.id;
        surface->cached.pop_front();

        for (auto& addon : surface->addons) {
            addon->apply(surface->applied);
        }
    }

    if (surface->applied == prev_applied_commit_id) return;

#if WROC_NOISY_COMMIT
    log_debug("Applied commits {} -> {}", prev_applied_commit_id, surface->applied);
#endif

    // Update buffer_src/buffer_dst

    if (surface->current.committed.contains(wroc_surface_committed_state::offset)) {
        surface->buffer_dst.origin += surface->current.delta;

        surface->current.committed -= wroc_surface_committed_state::offset;
        surface->current.delta = {};
    }

    if (surface->current.buffer) {
        surface->buffer_src = {{}, surface->current.buffer->extent, core::xywh};
        // TODO: inverse buffer_transform
        surface->buffer_dst.extent = vec2f64(surface->current.buffer->extent) / surface->current.buffer_scale;
    }

    // TODO: This logic should be in viewporter.cpp

    if (auto* viewport = wroc_surface_get_addon<wroc_viewport>(surface)) {
        auto& current = viewport->current;
        if (current.committed.contains(wroc_viewport_committed_state::source)) {
            surface->buffer_src = current.source;
        }
        if (current.committed.contains(wroc_viewport_committed_state::destination)) {
            surface->buffer_dst.extent = current.destination;
        } else if (current.committed.contains(wroc_viewport_committed_state::source)) {
            surface->buffer_dst.extent = current.source.extent;
        }

#if WROC_NOISY_COMMIT
        log_debug("buffer src = {}, dst = {}", core::to_string(surface->buffer_src), core::to_string(surface->buffer_dst));
#endif
    }

    // Handle map/unmap

    wroc_surface_update_map_state(surface);

    // Flush subsurface state recursively

    if (surface->current.surface_stack.size() > 1) {
        for (auto[s, _] : surface->current.surface_stack) {
            if (s.get() != surface) {
                wroc_surface_flush_apply(s.get());
            }
        }
    }
}

static
void surface_destroy(wl_client* client, wl_resource* resource)
{
    auto* surface = wroc_get_userdata<wroc_surface>(resource);

    surface_set_mapped(surface, false);

    wl_resource_destroy(resource);
}

const struct wl_surface_interface wroc_wl_surface_impl = {
    .destroy              = surface_destroy,
    .attach               = wroc_wl_surface_attach,
    WROC_STUB_QUIET(damage),
    .frame                = wroc_wl_surface_frame,
    .set_opaque_region    = wroc_wl_surface_set_opaque_region,
    .set_input_region     = wroc_wl_surface_set_input_region,
    .commit               = wroc_wl_surface_commit,
    WROC_STUB(set_buffer_transform),
    WROC_STUB_QUIET(set_buffer_scale),
    WROC_STUB_QUIET(damage_buffer),
    .offset               = wroc_wl_surface_offset,
};

wroc_surface_state::~wroc_surface_state()
{
    // TODO: Should we send a done instead?
    while (auto* callback = frame_callbacks.front()) {
        wl_resource_destroy(callback);
    }
}

wroc_surface::~wroc_surface()
{
    std::erase(server->surfaces, this);

    log_debug("Surface {} was destroyed", (void*)this);
}

bool wroc_surface_point_accepts_input(wroc_surface* surface, vec2f64 surface_pos)
{
    rect2f64 buffer_rect = {};
    if (surface->current.buffer) {
        buffer_rect.extent = vec2f64{surface->current.buffer->extent} / surface->current.buffer_scale;
    }

    // log_debug("buffer_rect = {}", core::to_string(buffer_rect));

    if (!core::rect::contains(buffer_rect, surface_pos)) return false;

    auto accepts_input = surface->current.input_region.contains(surface_pos);

    // log_trace("input_region.contains{} = {}", core::to_string(point), accepts_input);

    return accepts_input;
}

void wroc_surface_raise(wroc_surface* surface)
{
    // TODO: Implement generic "slide" algorithm for this and subsurface layers

    auto i = std::ranges::find(server->surfaces, surface);
    std::rotate(i, i + 1, server->surfaces.end());
}

rect2f64 wroc_surface_get_frame(wroc_surface* surface)
{
    switch (surface->role) {
        break;case wroc_surface_role::cursor:
            // Consider only hotspot as pointer's "frame"
            return {server->seat->pointer->position, vec2f64(1.0), core::xywh};
        break;case wroc_surface_role::xdg_toplevel:
            if (auto* toplevel = wroc_surface_get_addon<wroc_toplevel>(surface)) {
                return wroc_toplevel_get_layout_rect(toplevel);
            }
        break;case wroc_surface_role::xdg_popup: {
            if (auto* xdg_surface = wroc_surface_get_addon<wroc_xdg_surface>(surface)) {
                auto space = wroc_surface_get_coord_space(surface);
                aabb2f64 geom = wroc_xdg_surface_get_geometry(xdg_surface);
                return { space.to_global(geom.min), space.to_global(geom.max), core::minmax };
            }
        }
        break;default: {
            auto space = wroc_surface_get_coord_space(surface);
            aabb2f64 buffer_dst = surface->buffer_dst;
            return { space.to_global(buffer_dst.min), space.to_global(buffer_dst.max), core::minmax };
        }
    }

    log_error("Surface with role \"{}\" has no valid frame!", core::to_string(surface->role));
    return {};
}

wroc_coord_space wroc_surface_get_coord_space(wroc_surface* surface)
{
    switch (surface->role) {
        break;case wroc_surface_role::none:
            ;
        break;case wroc_surface_role::cursor:
              case wroc_surface_role::drag_icon:
            return {server->seat->pointer->position, vec2f64(1.0)};
        break;case wroc_surface_role::subsurface:
            if (auto* subsurface = static_cast<wroc_subsurface*>(surface->role_addon.get()); subsurface && subsurface->parent) {
                auto space = wroc_surface_get_coord_space(subsurface->parent.get());
                return {space.origin + vec2f64(subsurface->position()) * space.scale, space.scale};
            }
        break;case wroc_surface_role::xdg_popup:
            if (auto* popup = wroc_surface_get_addon<wroc_popup>(surface); popup && popup->parent) {
                auto space = wroc_surface_get_coord_space(popup->parent->surface.get());
                auto parent_geom = wroc_xdg_surface_get_geometry(popup->parent.get());
                auto geom = wroc_xdg_surface_get_geometry(popup->base());
                return {space.origin + (popup->position + vec2f64(parent_geom.origin) - vec2f64(geom.origin)) * space.scale, space.scale};
            }
        break;case wroc_surface_role::xdg_toplevel:
            if (auto* toplevel = wroc_surface_get_addon<wroc_toplevel>(surface)) {
                rect2i32 geom;
                auto layout = wroc_toplevel_get_layout_rect(toplevel, &geom);
                auto fit = core::rect::fit<f64>(layout.extent, geom.extent);
                auto scale = fit.extent / vec2f64(geom.extent);
                auto pos = layout.origin + fit.origin - vec2f64(geom.origin) * scale;
                return {pos, scale};
            }
    }

    log_error("Surface with role \"{}\" has no valid position!", core::to_string(surface->role));
    return {{}, {1, 1}};
}

vec2f64 wroc_surface_pos_from_global(wroc_surface* surface, vec2f64 global_pos)
{
    return wroc_surface_get_coord_space(surface).from_global(global_pos);
}

vec2f64 wroc_surface_pos_to_global(wroc_surface* surface, vec2f64 surface_pos)
{
    return wroc_surface_get_coord_space(surface).to_global(surface_pos);
}

// -----------------------------------------------------------------------------

bool wroc_surface_put_addon_impl(wroc_surface* surface, wroc_surface_addon* addon, wroc_surface_role role)
{
    if (role != wroc_surface_role::none) {
        if (surface->role_addon) {
            log_error("Surface already has addon for role {}", core::to_string(surface->role));
            return false;
        }

        if (surface->role == wroc_surface_role::none) {
            surface->role = role;
        } else if (surface->role != role) {
            log_error("Surface already has role {}, can't change to {}", core::to_string(surface->role), core::to_string(role));
            return false;
        }

        surface->role_addon = addon;
    }

    addon->surface = surface;
    surface->addons.emplace_back(addon);

    return true;
}

wroc_surface_addon* wroc_surface_get_role_addon(wroc_surface* surface, wroc_surface_role role)
{
    if (!surface) return nullptr;
    return surface->role == role ? surface->role_addon.get() : nullptr;
}

wroc_surface_addon* wroc_surface_get_addon(wroc_surface* surface, const std::type_info& type)
{
    if (!surface) return nullptr;
    auto iter = std::ranges::find_if(surface->addons, [&](const auto& a) { return typeid(*a.get()) == type; });
    return iter == surface->addons.end() ? nullptr : iter->get();
}

void wroc_surface_addon_detach(wroc_surface_addon* addon)
{
    if (!addon->surface) return;

    auto* surface = addon->surface.get();

    if (surface->role_addon == addon) {
        surface->role_addon = nullptr;
        surface_set_mapped(surface, false);
    }
    addon->surface = nullptr;

    std::erase_if(surface->addons, [&](const auto& a) { return a.get() == addon; });
}
