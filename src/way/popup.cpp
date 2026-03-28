#include "internal.hpp"

struct WayAxisRegion
{
    i32 pos;
    i32 size;
};

struct WayAxisOverlaps
{
    i32 start;
    i32 end;
};

struct WayPositionerRules
{
    vec2i32 size;
    rect2i32 anchor_rect;
    xdg_positioner_anchor anchor;
    xdg_positioner_gravity gravity;
    Flags<xdg_positioner_constraint_adjustment> constraint_adjustment;
    vec2i32 offset;
    bool reactive = false;
    vec2i32 parent_size;
    u32 parent_configure;
};

struct WayPositioner : WayObject
{
    WayServer* server;

    WayResource resource;

    WayPositionerRules rules;
};

void way_create_positioner(wl_client* client, wl_resource* resource, u32 id)
{
    auto positioner = ref_create<WayPositioner>();
    positioner->resource = way_resource_create_refcounted(xdg_positioner, client, resource, id, positioner.get());
}

#define SET(Name, Expr, ...) \
    .set_##Name = [](wl_client* client, wl_resource* resource __VA_OPT__(,) __VA_ARGS__) { \
        way_get_userdata<WayPositioner>(resource)->rules.Name = (Expr); \
    }

WAY_INTERFACE(xdg_positioner) = {
    .destroy = way_simple_destroy,
    SET(size,                  vec2i32(width, height),                  i32 width, i32 height),
    SET(anchor_rect,           rect2i32({x, y}, {width, height}, xywh), i32 x, i32 y, i32 width, i32 height),
    SET(anchor,                xdg_positioner_anchor(anchor),           u32 anchor),
    SET(gravity,               xdg_positioner_gravity(gravity),         u32 gravity),
    SET(constraint_adjustment, xdg_positioner_constraint_adjustment(constraint_adjustment), u32 constraint_adjustment),
    SET(offset,                vec2i32(x, y),                           i32 x, i32 y),
    SET(reactive,              true),
    SET(parent_size,           vec2i32(width, height),                  i32 width, i32 height),
    SET(parent_configure,      serial,                                  u32 serial),
};

#undef SET

struct WayPositionerAxisRules
{
    WayAxisRegion anchor;
    i32 size;
    i32 gravity;
    bool flip;
    bool slide;
    bool resize;
};

#define WAY_NOISY_POSITIONERS 1

static
WayAxisRegion positioner_apply_axis(const WayPositionerAxisRules& rules, WayAxisRegion constraint)
{
#if WAY_NOISY_POSITIONERS
    log_debug("way_xdg_position_apply_axis");
    log_debug("  constraint = ({} ; {})", constraint.pos, constraint.size);
    log_debug("  anchor = ({} ; {})", rules.anchor.pos, rules.anchor.size);
    log_debug("  size, gravity  = {}, {}", rules.size, rules.gravity);
    log_debug("  flip   = {}", rules.flip);
    log_debug("  slide  = {}", rules.slide);
    log_debug("  resize = {}", rules.resize);
#endif

    auto get_position = [](auto& rules) -> WayAxisRegion {
        return {rules.anchor.pos + rules.gravity - rules.size, rules.size};
    };

    auto get_overlaps = [&](WayAxisRegion region) -> WayAxisOverlaps {
        return {constraint.pos - region.pos, region.pos + region.size - (constraint.pos + constraint.size)};
    };

    auto is_unconstrained = [&](WayAxisRegion region) {
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

#define EDGES_TO_REL_CASES(Prefix) \
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
Vec<2, T> positioner_anchor_to_rel(xdg_positioner_anchor anchor, Vec<2, T> rel)
{
    switch (anchor) {
        EDGES_TO_REL_CASES(XDG_POSITIONER_ANCHOR)
    }

    debug_unreachable();
}

template<typename T>
Vec<2, T> positioner_gravity_to_rel(xdg_positioner_gravity gravity, Vec<2, T> rel)
{
    switch (gravity) {
        EDGES_TO_REL_CASES(XDG_POSITIONER_GRAVITY)
    }

    debug_unreachable();
}

#undef EDGES_TO_REL_CASES

static
void positioner_apply_axis_from_rules(const WayPositionerRules& rules, rect2i32 constraint, rect2i32& target, u32 axis)
{
    auto anchor = positioner_anchor_to_rel(rules.anchor, rules.anchor_rect.extent);
    auto gravity = positioner_gravity_to_rel(rules.gravity, rules.size);
    static constexpr std::array flip_adjustments   = { XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_X,   XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_Y   };
    static constexpr std::array slide_adjustments  = { XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_X,  XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_Y  };
    static constexpr std::array resize_adjustments = { XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_RESIZE_X, XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_RESIZE_Y };
    WayPositionerAxisRules axis_rules {
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
    WayAxisRegion constraint_region {
        .pos = constraint.origin[axis] - offset,
        .size = constraint.extent[axis],
    };
    auto region = positioner_apply_axis(axis_rules, constraint_region);
    target.origin[axis] = region.pos + offset;
    target.extent[axis] = region.size;
}

static
rect2i32 positioner_apply(const WayPositionerRules& rules, rect2i32 constraint)
{
    rect2i32 target;
    positioner_apply_axis_from_rules(rules, constraint, target, 0);
    positioner_apply_axis_from_rules(rules, constraint, target, 1);
    return target;
}

// -----------------------------------------------------------------------------

static
void popup_update_geometry(WaySurface* surface)
{
    auto position    = surface->popup.position;
    auto geom        = surface->current.xdg.geometry;
    auto parent_geom = surface->parent->current.xdg.geometry;
    scene_tree_set_translation(surface->scene.tree.get(),
        position + vec2f32(parent_geom.origin) - vec2f32(geom.origin));
}

static
void position(WaySurface* surface, const WayPositionerRules& rules, std::optional<u32> token = std::nullopt)
{
    auto* server = surface->client->server;

    rect2i32 constraint = {{-INT_MAX/4, -INT_MAX/4}, {INT_MAX/2, INT_MAX/2}, xywh};

    {
        auto anchor = rules.anchor_rect;
        auto point = vec2f32(anchor.origin) + vec2f32(anchor.extent) * 0.5f;
        if (auto* output = scene_find_output_for_point(server->scene, point).output) {
            aabb2f32 vp = scene_output_get_viewport(output);
            auto translation = scene_tree_get_position(surface->parent->scene.tree.get());
            constraint = {
                vp.min - translation,
                vp.max - translation,
                minmax
            };
        }
    }

    auto geometry = positioner_apply(rules, constraint);
    log_debug("popup geometry: {}", geometry);
    surface->popup.position = geometry.origin;

    if (token) {
        way_send(server, xdg_popup_send_repositioned, surface->popup.resource, *token);
    }
    way_send(server, xdg_popup_send_configure, surface->popup.resource,
        geometry.origin.x, geometry.origin.y, geometry.extent.x, geometry.extent.y);
    way_xdg_surface_configure(surface);
    popup_update_geometry(surface);
}

void way_get_popup(wl_client* client, wl_resource* resource, u32 id, wl_resource* wl_parent, wl_resource* positioner)
{
    auto* surface = way_get_userdata<WaySurface>(resource);
    surface->role = WaySurfaceRole::xdg_popup;
    surface->popup.resource = way_resource_create_refcounted(xdg_popup, client, resource, id, surface);

    auto* parent = way_get_userdata<WaySurface>(wl_parent);
    surface->parent = parent;

    // Place into parent's surface stack
    scene_tree_place_above(parent->scene.tree.get(), nullptr, surface->scene.tree.get());

    position(surface, way_get_userdata<WayPositioner>(positioner)->rules);
}

void way_popup_apply(WaySurface* surface, WaySurfaceState& from)
{
    popup_update_geometry(surface);
}

static
void reposition(wl_client* client, wl_resource* resource, wl_resource* positioner, u32 token)
{
    auto* surface = way_get_userdata<WaySurface>(resource);
    position(surface, way_get_userdata<WayPositioner>(positioner)->rules, token);
}

WAY_INTERFACE(xdg_popup) = {
    .destroy = way_role_destroy,
    WAY_STUB(grab),
    .reposition = reposition,
};
