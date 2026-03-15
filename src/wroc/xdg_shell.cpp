#include "protocol.hpp"

#include "core/math.hpp"

const u32 wroc_xdg_wm_base_version = 7;

static
void create_positioner(wl_client* client, wl_resource* resource, u32 id);

static
void get_xdg_surface(wl_client* client, wl_resource* resource, u32 id, wl_resource* wl_surface)
{
    auto* new_resource = wroc_resource_create(client, &xdg_surface_interface, wl_resource_get_version(resource), id);
    auto* surface = wroc_get_userdata<wroc_surface>(wl_surface);;
    auto* xdg_surface = core::create_unsafe<wroc_xdg_surface>();
    xdg_surface->resource = new_resource;
    wroc_surface_put_addon(surface, xdg_surface);
    wroc_resource_set_implementation_refcounted(new_resource, &wroc_xdg_surface_impl, xdg_surface);
}

const struct xdg_wm_base_interface wroc_xdg_wm_base_impl = {
    .destroy           = wroc_simple_resource_destroy_callback,
    .create_positioner = create_positioner,
    .get_xdg_surface   = get_xdg_surface,
    WROC_STUB(pong),
};

void wroc_xdg_wm_base_bind_global(wl_client* client, void* data, u32 version, u32 id)
{
    auto* new_resource = wroc_resource_create(client, &xdg_wm_base_interface, version, id);
    wroc_resource_set_implementation(new_resource, &wroc_xdg_wm_base_impl, nullptr);
};

// -----------------------------------------------------------------------------

static
void get_toplevel(wl_client* client, wl_resource* resource, u32 id)
{
    auto* new_resource = wroc_resource_create(client, &xdg_toplevel_interface, wl_resource_get_version(resource), id);
    auto* xdg_surface = wroc_get_userdata<wroc_xdg_surface>(resource);
    auto* xdg_toplevel = core::create_unsafe<wroc_toplevel>();
    xdg_toplevel->resource = new_resource;
    wroc_surface_put_addon(xdg_surface->surface.get(), xdg_toplevel);
    wroc_resource_set_implementation_refcounted(new_resource, &wroc_xdg_toplevel_impl, xdg_toplevel);
}

static
void set_window_geometry(wl_client* client, wl_resource* resource, i32 x, i32 y, i32 width, i32 height)
{
    auto* surface = wroc_get_userdata<wroc_xdg_surface>(resource);
    surface->pending->geometry = {{x, y}, {width, height}, core::xywh};
    surface->pending->committed |= wroc_xdg_surface_committed_state::geometry;
}

static
aabb2i32 compute_fallback_geometry(wroc_surface* surface)
{
    aabb2i32 bounds = surface->buffer_dst;

    auto expand_bounds = [&](this auto&& expand_bounds, vec2i32 pos, wroc_surface* s) -> void {
        aabb2i32 dst = s->buffer_dst;

        bounds.min = glm::min(bounds.min, dst.min + pos);
        bounds.max = glm::max(bounds.max, dst.max + pos);

        for (auto&[ss, position] : s->current.surface_stack) {
            if (!ss || ss.get() == s) continue;
            if (auto* subsurface = wroc_surface_get_addon<wroc_subsurface>(ss.get())) {
                expand_bounds(pos + subsurface->position(), ss.get());
            }
        }
    };

    expand_bounds({}, surface);

    return bounds;
}

void wroc_xdg_surface::commit(wroc_commit_id id)
{
    wroc_surface_state_queue_commit(this, id);
}

static
void xdg_surface_apply(wroc_xdg_surface* self, wrox_xdg_surface_state& from, wroc_commit_id id)
{
    auto& to = self->current;

    if (from.committed.contains(wroc_xdg_surface_committed_state::geometry)) {
        if (!from.geometry.extent.x || !from.geometry.extent.y) {
            log_error("Zero size invalid geometry committed, treating as if geometry never set!");
            to.committed -= wroc_xdg_surface_committed_state::geometry;
            from.committed -= wroc_xdg_surface_committed_state::geometry;
        } else {
            to.geometry = from.geometry;
        }
    }

    if (from.committed.contains(wroc_xdg_surface_committed_state::ack)) {
        to.acked_serial = from.acked_serial;
    }

    to.committed |= from.committed;
}

void wroc_xdg_surface::apply(wroc_commit_id id)
{
    wroc_surface_state_queue_apply(this, id, xdg_surface_apply);
}

static
void ack_configure(wl_client* client, wl_resource* resource, u32 serial)
{
    auto* xdg_surface = wroc_get_userdata<wroc_xdg_surface>(resource);

    if (serial > xdg_surface->sent_serial) {
        wroc_post_error(xdg_surface->resource, XDG_SURFACE_ERROR_INVALID_SERIAL,
            "Client acked configure {} which is higher than latest sent sent configure {}",
            serial, xdg_surface->sent_serial);
        return;
    }

    if (serial <= xdg_surface->acked_serial) {
        log_warn("Client acked old configure serial");
        return;
    }

    log_info("Client acked configure: {}", serial);
    xdg_surface->pending->acked_serial = serial;
    xdg_surface->pending->committed |= wroc_xdg_surface_committed_state::ack;

    xdg_surface->acked_serial = serial;

    // TODO: We are now flushing all configured state on ack for correctness (previously we flushed on commit)
    //       This prevents some applications from deadlocking, but may result in sending too many resize
    //       requests to applications that eagerly flush asynchronously from resizing.
    //       As such we should throttle resizes separately.
    if (auto* toplevel = wroc_surface_get_addon<wroc_toplevel>(xdg_surface->surface.get())) {
        wroc_toplevel_flush_configure(toplevel);
    }
}

void wroc_xdg_surface_flush_configure(wroc_xdg_surface* xdg_surface)
{
    xdg_surface->sent_serial = wl_display_next_serial(server->display);
    wroc_send(xdg_surface_send_configure, xdg_surface->resource, xdg_surface->sent_serial);
}

static
void get_popup(wl_client* client, wl_resource* resource, u32 id, wl_resource* parent, wl_resource* positioner);

const struct xdg_surface_interface wroc_xdg_surface_impl = {
    .destroy             = wroc_simple_resource_destroy_callback,
    .get_toplevel        = get_toplevel,
    .get_popup           = get_popup,
    .set_window_geometry = set_window_geometry,
    .ack_configure       = ack_configure,
};

rect2i32 wroc_xdg_surface_get_geometry(wroc_xdg_surface* xdg_surface)
{
    // TODO: Clamp geometry always?
    return (xdg_surface->current.committed.contains(wroc_xdg_surface_committed_state::geometry))
        ? xdg_surface->current.geometry
        : rect2i32(compute_fallback_geometry(xdg_surface->surface.get()));
}

rect2f64 wroc_toplevel_get_layout_rect(wroc_toplevel* toplevel, rect2i32* p_geometry)
{
    auto geom = wroc_xdg_surface_get_geometry(toplevel->base());
    if (p_geometry) *p_geometry = geom;

    if (toplevel->fullscreen.output) {
        return toplevel->fullscreen.output->layout_rect;
    }

    auto extent = toplevel->layout_size ? *toplevel->layout_size : vec2f64(geom.extent);
    auto offset = toplevel->anchor.position - extent * toplevel->anchor.relative;
    return {offset, extent, core::xywh};
}

// -----------------------------------------------------------------------------

static
void toplevel_set_title(wl_client* client, wl_resource* resource, const char* title)
{
    auto* toplevel = wroc_get_userdata<wroc_toplevel>(resource);
    toplevel->pending->title = title ? std::string{title} : std::string{};
    toplevel->pending->committed |= wroc_xdg_toplevel_committed_state::title;
}

static
void toplevel_set_app_id(wl_client* client, wl_resource* resource, const char* app_id)
{
    auto* toplevel = wroc_get_userdata<wroc_toplevel>(resource);
    toplevel->pending->app_id = app_id ? std::string{app_id} : std::string{};
    toplevel->pending->committed |= wroc_xdg_toplevel_committed_state::app_id;
}

static
void toplevel_move(wl_client* client, wl_resource* resource, wl_resource* seat, u32 serial)
{
    // TODO: Check serial

    auto* toplevel = wroc_get_userdata<wroc_toplevel>(resource);

    // TODO: Use seat to select pointer
    auto* pointer = server->seat->pointer.get();
    if (!pointer->pressed.contains(BTN_LEFT)) {
        log_warn("toplevel attempted to initiate move but left button was not pressed");
        return;
    }

    wroc_begin_move_interaction(toplevel, pointer, wroc_direction::horizontal | wroc_direction::vertical);
}

void wroc_toplevel_set_anchor_relative(wroc_toplevel* toplevel, vec2f64 anchor_rel)
{
    toplevel->anchor.position += vec2f64(anchor_rel - toplevel->anchor.relative) * wroc_toplevel_get_layout_rect(toplevel).extent;
    toplevel->anchor.relative = anchor_rel;
}

static
void toplevel_resize(wl_client* client, wl_resource* resource, wl_resource* seat, u32 serial, u32 wl_edges)
{
    // TODO: Check serial

    auto* toplevel = wroc_get_userdata<wroc_toplevel>(resource);

    // TODO: Use seat to select pointer
    auto* pointer = server->seat->pointer.get();
    if (!pointer->pressed.contains(BTN_LEFT)) {
        log_warn("toplevel attempted to initiate resize but left button was not pressed");
        return;
    }

    core::Flags<wroc_edge> edges;
    switch (wl_edges) {
        break;case XDG_TOPLEVEL_RESIZE_EDGE_NONE: return;
        break;case XDG_TOPLEVEL_RESIZE_EDGE_TOP:          edges = wroc_edge::top;
        break;case XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM:       edges = wroc_edge::bottom;
        break;case XDG_TOPLEVEL_RESIZE_EDGE_LEFT:         edges = wroc_edge::left;
        break;case XDG_TOPLEVEL_RESIZE_EDGE_RIGHT:        edges = wroc_edge::right;
        break;case XDG_TOPLEVEL_RESIZE_EDGE_TOP_LEFT:     edges = wroc_edge::top | wroc_edge::left;
        break;case XDG_TOPLEVEL_RESIZE_EDGE_TOP_RIGHT:    edges = wroc_edge::top | wroc_edge::right;
        break;case XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_LEFT:  edges = wroc_edge::bottom | wroc_edge::left;
        break;case XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_RIGHT: edges = wroc_edge::bottom | wroc_edge::right;
    }

    wroc_toplevel_set_anchor_relative(toplevel, wroc_edges_to_relative(wroc_edges_inverse(edges)));
    wroc_begin_resize_interaction(toplevel, pointer, edges);
}

static
void toplevel_set_fullscreen(wl_client* client, wl_resource* resource, wl_resource* requested_output)
{
    auto* toplevel = wroc_get_userdata<wroc_toplevel>(resource);
    wroc_output* output;
    wroc_output_layout_clamp_position(server->output_layout.get(), server->seat->pointer->position, &output);
    wroc_toplevel_set_fullscreen(toplevel, output);
}

static
void toplevel_unset_fullscreen(wl_client* client, wl_resource* resource)
{
    auto* toplevel = wroc_get_userdata<wroc_toplevel>(resource);
    wroc_toplevel_set_fullscreen(toplevel, nullptr);
}

static
void toplevel_set_parent(wl_client* client, wl_resource* resource, wl_resource* parent)
{
    auto* toplevel = wroc_get_userdata<wroc_toplevel>(resource);
    toplevel->pending->parent = wroc_get_userdata<wroc_toplevel>(parent);
    toplevel->pending->committed |= wroc_xdg_toplevel_committed_state::parent;
}

static
void toplevel_on_initial_commit(wroc_toplevel* toplevel)
{
    wroc_toplevel_set_size(toplevel, {0, 0});
    wroc_toplevel_set_state(toplevel, XDG_TOPLEVEL_STATE_ACTIVATED, true);

    wroc_toplevel_flush_configure(toplevel);

    if (wl_resource_get_version(toplevel->resource) >= XDG_TOPLEVEL_WM_CAPABILITIES_SINCE_VERSION) {
        wroc_send(xdg_toplevel_send_wm_capabilities, toplevel->resource, core::ptr_to(wroc_to_wl_array<const xdg_toplevel_wm_capabilities>({
            XDG_TOPLEVEL_WM_CAPABILITIES_FULLSCREEN,
        })));
    }

    wroc_xdg_surface_flush_configure(toplevel->base());
}

static
void toplevel_clamp_to_layout(wroc_toplevel* toplevel)
{
    // Find output for toplevel
    auto* output = wroc_output_layout_output_for_surface(server->output_layout.get(), toplevel->surface.get());
    if (!output) return;

    auto frame = wroc_toplevel_get_layout_rect(toplevel);
    auto bounds = output->layout_rect;

    // Constrain bounds by layout padding
    auto& c = server->zone.config;
    bounds.origin += vec2f64{c.external_padding.left, c.external_padding.top};
    bounds.extent -= vec2f64{
        c.external_padding.left + c.external_padding.right,
        c.external_padding.top + c.external_padding.bottom
    };

    // Constrain
    auto constrained = core::rect::constrain(frame, bounds);

    // Apply
    wroc_toplevel_set_size(toplevel, constrained.extent);
    toplevel->anchor.position = constrained.origin;
    toplevel->anchor.relative = {};
}

static
void toplevel_anchor_to_parent(wroc_toplevel* toplevel)
{
    if (!toplevel->current.parent || !toplevel->current.parent->surface) return;

    rect2f64 parent_frame = wroc_surface_get_frame(toplevel->current.parent->surface.get());
    toplevel->anchor.position = parent_frame.origin + parent_frame.extent * 0.5;
    toplevel->anchor.relative = wroc_edges_to_relative({});

    if (toplevel->initial_size_received) {
        toplevel_clamp_to_layout(toplevel);
    }

    // TODO: Ensure child windows are layered on top of their parents
}

static
void toplevel_on_initial_size(wroc_toplevel* toplevel)
{
    auto geom = wroc_xdg_surface_get_geometry(toplevel->base());
    log_debug("Initial surface size: {}", core::to_string(geom.extent));
    wroc_toplevel_set_size(toplevel, geom.extent);
    wroc_toplevel_flush_configure(toplevel);
}

void wroc_toplevel::commit(wroc_commit_id id)
{
    wroc_surface_state_queue_commit(this, id);
}

static
void toplevel_apply(wroc_toplevel* self, wroc_xdg_toplevel_state& from, wroc_commit_id id)
{
    auto& to = self->current;

    if (from.committed.contains(wroc_xdg_toplevel_committed_state::title))  to.title  = from.title;
    if (from.committed.contains(wroc_xdg_toplevel_committed_state::app_id)) to.app_id = from.app_id;
    if (from.committed.contains(wroc_xdg_toplevel_committed_state::parent)) {
        to.parent = from.parent;
        toplevel_anchor_to_parent(self);
    }

    to.committed |= from.committed;
}

void wroc_toplevel::apply(wroc_commit_id id)
{
    wroc_surface_state_queue_apply(this, id, toplevel_apply);

    if (!initial_configure_complete) {
        initial_configure_complete = true;
        toplevel_on_initial_commit(this);
    }
    else if (!initial_size_received) {
        initial_size_received = true;
        toplevel_on_initial_size(this);
    }
}

void wroc_toplevel::on_mapped_change()
{
    if (surface->mapped) {
        if (fullscreen.output) {
            wroc_toplevel_update_fullscreen_size(this);
        } else {
            // Center mapped toplevels on cursor
            // TODO: For initial client windows, use cursor position from when client was first launched?
            if (!current.parent) {
                log_debug("Centering mapped toplevel on cursor");

                anchor.position = server->seat->pointer->position;
                anchor.relative = wroc_edges_to_relative({});
            }

            toplevel_clamp_to_layout(this);
        }

        // Newly mapped toplevels pull focus
        // TODO: Configurable handling for this?
        wroc_keyboard_enter(server->seat->keyboard.get(), surface.get());
    } else {
        if (this == server->movesize.grabbed_toplevel.get()) {
            server->movesize.grabbed_toplevel = nullptr;
            // TODO: We'll need a function for cancelling more complex interactions
            server->interaction_mode = wroc_interaction_mode::normal;
        }
    }
}

const struct xdg_toplevel_interface wroc_xdg_toplevel_impl = {
    .destroy    = wroc_simple_resource_destroy_callback,
    .set_parent = toplevel_set_parent,
    .set_title  = toplevel_set_title,
    .set_app_id = toplevel_set_app_id,
    WROC_STUB(show_window_menu),
    .move       = toplevel_move,
    .resize     = toplevel_resize,
    // TODO: These should still be implemented, just silencing for now!
    WROC_STUB_QUIET(set_max_size),
    WROC_STUB_QUIET(set_min_size),
    WROC_STUB(set_maximized),
    WROC_STUB(unset_maximized),
    .set_fullscreen   = toplevel_set_fullscreen,
    .unset_fullscreen = toplevel_unset_fullscreen,
    WROC_STUB(set_minimized),
};

void wroc_toplevel_set_size(wroc_toplevel* toplevel, vec2i32 size)
{
    auto& configure = toplevel->configure;
    if (configure.size == size) return;
    configure.size = size;
    configure.pending = wroc_xdg_toplevel_configure_state::size;
}

void wroc_toplevel_set_bounds(wroc_toplevel* toplevel, vec2i32 bounds)
{
    if (wl_resource_get_version(toplevel->resource) >= XDG_TOPLEVEL_CONFIGURE_BOUNDS_SINCE_VERSION) {
        toplevel->configure.bounds = bounds;
        toplevel->configure.pending |= wroc_xdg_toplevel_configure_state::bounds;
    }
}

void wroc_toplevel_set_state(wroc_toplevel* toplevel, xdg_toplevel_state state, bool enabled)
{
    auto& configure = toplevel->configure;
    if (enabled) {
        if (!std::ranges::contains(configure.states, state)) {
            configure.states.emplace_back(state);
            configure.pending |= wroc_xdg_toplevel_configure_state::states;
        }
    } else if (std::erase(configure.states, state)) {
        configure.pending |= wroc_xdg_toplevel_configure_state::states;
    }
}

void wroc_toplevel_flush_configure(wroc_toplevel* toplevel)
{
    auto& configure = toplevel->configure;
    if (configure.pending.empty()) return;

    // TODO: We probably shouldn't always wait for a commit after an ack_configure?
    //       If the surface acks and then never submits, we would softlock on further configures
    if (toplevel->base()->sent_serial > toplevel->base()->acked_serial) {
        log_warn("Waiting for client ack before reconfiguring");
        return;
    }

    if (configure.pending.contains(wroc_xdg_toplevel_configure_state::bounds)) {
        wroc_send(xdg_toplevel_send_configure_bounds, toplevel->resource, configure.bounds.x, configure.bounds.y);
    }

    wroc_send(xdg_toplevel_send_configure, toplevel->resource, configure.size.x, configure.size.y,
        core::ptr_to(wroc_to_wl_array<xdg_toplevel_state>(configure.states)));

    wroc_xdg_surface_flush_configure(toplevel->base());

    configure.pending = {};
}

void wroc_toplevel_close(wroc_toplevel* toplevel)
{
    wroc_send(xdg_toplevel_send_close, toplevel->resource);
}

void wroc_toplevel_force_rescale(wroc_toplevel* toplevel, bool force_rescale)
{
    if (toplevel->layout_size.has_value() == force_rescale) return;

    if (force_rescale) {
        toplevel->layout_size = wroc_xdg_surface_get_geometry(toplevel->base()).extent;
    } else {
        if (toplevel->fullscreen.output) {
            wroc_toplevel_update_fullscreen_size(toplevel);
        } else if (toplevel->layout_size) {
            wroc_toplevel_set_size(toplevel, *toplevel->layout_size);
            wroc_toplevel_flush_configure(toplevel);
        }
        toplevel->layout_size = std::nullopt;
    }
}

void wroc_toplevel_set_layout_size(wroc_toplevel* toplevel, vec2i32 size)
{
    if (toplevel->layout_size) {
        toplevel->layout_size = size;
    } else {
        wroc_toplevel_set_size(toplevel, size);
    }
}

void wroc_toplevel_set_fullscreen(wroc_toplevel* toplevel, wroc_output* output)
{
    wroc_toplevel_set_state(toplevel, XDG_TOPLEVEL_STATE_FULLSCREEN, output);

    if (output && !toplevel->fullscreen.output) {
        // Set previous size when first setting fullscreen output
        toplevel->fullscreen.prev_size = wroc_toplevel_get_layout_rect(toplevel).extent;
    }

    if (toplevel->fullscreen.output && !output) {
        // Restore previous size on dropping out of fullscreen
        auto size = toplevel->fullscreen.prev_size;
        if (size.x && size.y) wroc_toplevel_set_layout_size(toplevel, size);
    }

    toplevel->fullscreen.output = output;

    if (output && !toplevel->layout_size) {
        wroc_toplevel_update_fullscreen_size(toplevel);
    }

    wroc_toplevel_flush_configure(toplevel);
}

void wroc_toplevel_update_fullscreen_size(wroc_toplevel* toplevel)
{
    if (toplevel->layout_size) return;
    if (!toplevel->fullscreen.output) return;

    wroc_toplevel_set_size(toplevel, toplevel->fullscreen.output->layout_rect.extent);
    wroc_toplevel_flush_configure(toplevel);
}

// -----------------------------------------------------------------------------

void create_positioner(wl_client* client, wl_resource* resource, u32 id)
{
    auto* new_resource = wroc_resource_create(client, &xdg_positioner_interface, wl_resource_get_version(resource), id);
    auto* positioner = core::create_unsafe<wroc_positioner>();
    positioner->resource = new_resource;
    wroc_resource_set_implementation_refcounted(new_resource, &wroc_xdg_positioner_impl, positioner);
}

#define POSITIONER_SET(Name, Expr, ...) \
    .set_##Name = [](wl_client* client, wl_resource* resource __VA_OPT__(,) __VA_ARGS__) { \
        wroc_get_userdata<wroc_positioner>(resource)->rules.Name = (Expr); \
    }

const struct xdg_positioner_interface wroc_xdg_positioner_impl = {
    .destroy = wroc_simple_resource_destroy_callback,
    POSITIONER_SET(size,                  vec2i32(width, height),                       i32 width, i32 height),
    POSITIONER_SET(anchor_rect,           rect2i32({x, y}, {width, height}, core::xywh), i32 x, i32 y, i32 width, i32 height),
    POSITIONER_SET(anchor,                xdg_positioner_anchor(anchor),                u32 anchor),
    POSITIONER_SET(gravity,               xdg_positioner_gravity(gravity),              u32 gravity),
    POSITIONER_SET(constraint_adjustment, xdg_positioner_constraint_adjustment(constraint_adjustment), u32 constraint_adjustment),
    POSITIONER_SET(offset,                vec2i32(x, y),                                i32 x, i32 y),
    POSITIONER_SET(reactive,              true),
    POSITIONER_SET(parent_size,           vec2i32(width, height),                       i32 width, i32 height),
    POSITIONER_SET(parent_configure,      serial,                                       u32 serial),
};

#undef POSITIONER_SET

struct wroc_xdg_positioner_axis_rules
{
    wroc_axis_region anchor;
    i32 size;
    i32 gravity;
    bool flip;
    bool slide;
    bool resize;
};

#define WROC_NOISY_POSITIONERS 0

static
wroc_axis_region positioner_apply_axis(const wroc_xdg_positioner_axis_rules& rules, wroc_axis_region constraint)
{
#if WROC_NOISY_POSITIONERS
    log_debug("wroc_xdg_position_apply_axis");
    log_debug("  constraint = ({} ; {})", constraint.pos, constraint.size);
    log_debug("  anchor = ({} ; {})", rules.anchor.pos, rules.anchor.size);
    log_debug("  size, gravity  = {}, {}", rules.size, rules.gravity);
    log_debug("  flip   = {}", rules.flip);
    log_debug("  slide  = {}", rules.slide);
    log_debug("  resize = {}", rules.resize);
#endif

    auto get_position = [](auto& rules) -> wroc_axis_region {
        return {rules.anchor.pos + rules.gravity - rules.size, rules.size};
    };

    auto get_overlaps = [&](wroc_axis_region region) -> wroc_axis_overlaps {
        return {constraint.pos - region.pos, region.pos + region.size - (constraint.pos + constraint.size)};
    };

    auto is_unconstrained = [&](wroc_axis_region region) {
        auto overlaps = get_overlaps(region);
        return overlaps.start <= 0 && overlaps.end <= 0;
    };

    auto region = get_position(rules);

    if (is_unconstrained(region)) {
        return region;
    }

    static constexpr bool ignore_flip = false;
    static constexpr bool always_slide = false;

    if (rules.flip && !ignore_flip) {
        auto flipped_rules = rules;
        flipped_rules.anchor.pos = rules.anchor.size - rules.anchor.pos;
        flipped_rules.gravity = rules.size - rules.gravity;
        auto flipped = get_position(flipped_rules);
        if (is_unconstrained(flipped)) {
            return flipped;
        }
    }

    if (rules.slide || always_slide) {
        auto overlap = get_overlaps(region);
        if (overlap.start > 0 && overlap.end > 0) {
            // Overlaps both at start and end, move in direction of gravity until opposite edge is unconstrained
            if (rules.gravity == rules.size) {
                region.pos += overlap.start;
            } else if (rules.gravity == 0) {
                region.pos -= overlap.end;
            }
        } else if (overlap.start > 0) {
            // Overlaps at start, move forward
            region.pos += std::min(overlap.start, -overlap.end);
            return region;
        } else if (overlap.end > 0) {
            // Overlaps at end, move backward
            region.pos -= std::min(overlap.end, -overlap.start);
            return region;
        }
    }

    if (rules.resize) {
        auto overlap = get_overlaps(region);
        if (overlap.start > 0 && overlap.end > 0) {
            // Overlaps both, fit constraint exactly
            region = constraint;
        } else if (overlap.start > 0 && overlap.start < region.size) {
            // Overlaps start by less than size, clip start
            region.pos  += overlap.start;
            region.size -= overlap.start;
        } else if (overlap.end > 0 && overlap.end < region.size) {
            // Overlaps end by less than size, clip end
            region.size -= overlap.end;
        }
    }

    // Return best effort

    return region;
}

#define WROC_WAYLAND_EDGES_TO_REL_CASES(Prefix) \
    case Prefix##_NONE:         return {rel.x / 2, rel.y / 2}; \
    case Prefix##_TOP:          return {rel.x / 2, 0        }; \
    case Prefix##_BOTTOM:       return {rel.x / 2, rel.y    }; \
    case Prefix##_LEFT:         return {0,         rel.y / 2}; \
    case Prefix##_RIGHT:        return {rel.x,     rel.y / 2}; \
    case Prefix##_TOP_LEFT:     return {0,         0        }; \
    case Prefix##_TOP_RIGHT:    return {rel.x,     0        }; \
    case Prefix##_BOTTOM_LEFT:  return {0,         rel.y    }; \
    case Prefix##_BOTTOM_RIGHT: return {rel.x,     rel.y    };

template<typename T>
core::Vec<2, T> positioner_anchor_to_rel(xdg_positioner_anchor anchor, core::Vec<2, T> rel)
{
    switch (anchor) {
        WROC_WAYLAND_EDGES_TO_REL_CASES(XDG_POSITIONER_ANCHOR)
    }

    core::unreachable();
}

template<typename T>
core::Vec<2, T> positioner_gravity_to_rel(xdg_positioner_gravity gravity, core::Vec<2, T> rel)
{
    switch (gravity) {
        WROC_WAYLAND_EDGES_TO_REL_CASES(XDG_POSITIONER_GRAVITY)
    }

    core::unreachable();
}

static
void positioner_apply_axis_from_rules(const wroc_positioner_rules& rules, rect2i32 constraint, rect2i32& target, u32 axis)
{
    auto anchor = positioner_anchor_to_rel(rules.anchor, rules.anchor_rect.extent);
    auto gravity = positioner_gravity_to_rel(rules.gravity, rules.size);
    static constexpr std::array flip_adjustments   = { XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_X,   XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_Y   };
    static constexpr std::array slide_adjustments  = { XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_X,  XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_Y  };
    static constexpr std::array resize_adjustments = { XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_RESIZE_X, XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_RESIZE_Y };
    wroc_xdg_positioner_axis_rules axis_rules {
        .anchor = {
            .pos = anchor[axis],
            .size = rules.anchor_rect.extent[axis],
        },
        .size = rules.size[axis],
        .gravity = gravity[axis],
        .flip   = bool(rules.constraint_adjustment & flip_adjustments[axis]),
        .slide  = bool(rules.constraint_adjustment & slide_adjustments[axis]),
        .resize = bool(rules.constraint_adjustment & resize_adjustments[axis]),
    };
    auto offset = rules.anchor_rect.origin[axis] + rules.offset[axis];
    wroc_axis_region constraint_region {
        .pos = constraint.origin[axis] - offset,
        .size = constraint.extent[axis],
    };
    auto region = positioner_apply_axis(axis_rules, constraint_region);
    target.origin[axis] = region.pos + offset;
    target.extent[axis] = region.size;
}

static
rect2i32 positioner_apply(const wroc_positioner_rules& rules, rect2i32 constraint)
{
    rect2i32 target;
    positioner_apply_axis_from_rules(rules, constraint, target, 0);
    positioner_apply_axis_from_rules(rules, constraint, target, 1);
    return target;
}

// -----------------------------------------------------------------------------

#define WROC_NOISY_POPUP 0

void get_popup(wl_client* client, wl_resource* resource, u32 id, wl_resource* _parent, wl_resource* positioner)
{
    auto* new_resource = wroc_resource_create(client, &xdg_popup_interface, wl_resource_get_version(resource), id);
    auto* base = wroc_get_userdata<wroc_xdg_surface>(resource);
    auto* popup = core::create_unsafe<wroc_popup>();
    popup->resource = new_resource;
    wroc_surface_put_addon(base->surface.get(), popup);

    auto* xdg_positioner = wroc_get_userdata<wroc_positioner>(positioner);

    popup->positioner = xdg_positioner;
    popup->parent = wroc_get_userdata<wroc_xdg_surface>(_parent);
    if (popup->parent) {
        if (wroc_toplevel* toplevel = wroc_surface_get_addon<wroc_toplevel>(popup->parent->surface.get())) {
            popup->root_toplevel = toplevel;
        } else if (wroc_popup* parent_popup = wroc_surface_get_addon<wroc_popup>(popup->parent->surface.get())) {
            popup->root_toplevel = parent_popup->root_toplevel;
        }
    }

#if WROC_NOISY_POPUP
    auto& rules = xdg_positioner->rules;
    log_debug("xdg_popup<{}> created:", (void*)popup);
    log_debug("  size = {}", core::to_string(rules.size));
    log_debug("  anchor_rect = {}", core::to_string(rules.anchor_rect));
    log_debug("  anchor = {}", core::to_string(rules.anchor));
    log_debug("  gravity = {}", core::to_string(rules.gravity));
    log_debug("  adjustment = {}", core::to_string(rules.constraint_adjustment));
    log_debug("  offset = {}", core::to_string(rules.offset));
    log_debug("  reactive = {}", rules.reactive);
    log_debug("  parent_size = {}", core::to_string(rules.parent_size));
    log_debug("  parent_configure = {}", rules.parent_configure);
#endif

    wroc_resource_set_implementation_refcounted(new_resource, &wroc_xdg_popup_impl, popup);
}

static
void popup_position(wroc_popup* popup)
{
    auto* parent = popup->parent.get();
    auto* parent_surface = parent->surface.get();

    auto* base = popup->base();

    rect2i32 constraint = {};

    auto parent_geom = wroc_xdg_surface_get_geometry(popup->parent.get());

    {
        auto anchor_origin = wroc_surface_pos_to_global(parent_surface, popup->positioner->rules.anchor_rect.origin + parent_geom.origin);
        wroc_output* output;
        wroc_output_layout_clamp_position(server->output_layout.get(), anchor_origin, &output);
        if (output) {
            aabb2f64 rect = output->layout_rect;
            rect.min = wroc_surface_pos_from_global(parent_surface, rect.min) - vec2f64(parent_geom.origin);
            rect.max = wroc_surface_pos_from_global(parent_surface, rect.max) - vec2f64(parent_geom.origin);
            constraint = rect;
        }
    }
    auto geometry = positioner_apply(popup->positioner->rules, constraint);

    popup->position = geometry.origin;

    if (popup->reposition_token) {
        wroc_send(xdg_popup_send_repositioned, popup->resource, *popup->reposition_token);
        popup->reposition_token = std::nullopt;
    }

    wroc_send(xdg_popup_send_configure, popup->resource, geometry.origin.x, geometry.origin.y, geometry.extent.x, geometry.extent.y);
    wroc_xdg_surface_flush_configure(base);
}

void wroc_popup::commit(wroc_commit_id)
{
    // TODO: Move this to apply and sort out state

    if (!initial_configure_complete) {
        initial_configure_complete = true;

        if (parent) {
            popup_position(this);
        } else {
            log_error("xdg_popup has no parent set at commit time, can't configure");
        }

        wroc_surface_raise(surface.get());
    }
}

void wroc_popup::apply(wroc_commit_id)
{
    // TODO
}

static
void popup_reposition(wl_client* client, wl_resource* resource, wl_resource* _positioner, u32 token)
{
    log_debug("xdg_popup::reposition(token = {})", token);

    auto* popup = wroc_get_userdata<wroc_popup>(resource);
    auto* positioner = wroc_get_userdata<wroc_positioner>(_positioner);

    popup->positioner = positioner;
    popup->reposition_token = token;

    popup_position(popup);

    // TODO: We should double buffer the new geometry until the client has committed in response to the new configure
}

const struct xdg_popup_interface wroc_xdg_popup_impl = {
    .destroy = wroc_simple_resource_destroy_callback,
    WROC_STUB(grab),
    .reposition = popup_reposition,
};
