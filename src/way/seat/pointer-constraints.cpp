#include "seat.hpp"
#include "../server.hpp"
#include "../client.hpp"

#include "../surface/surface.hpp"
#include "../surface/region.hpp"

#include "../shell/shell.hpp"

struct WayPointerConstraint : WaySurfaceAddon
{
    WayResource resource;

    Ref<WmPointerConstraint> constraint;

    virtual void commit(WayCommitId) final override {}
    virtual void apply( WayCommitId) final override {}
};

static
void constrain_pointer(
    WmPointerConstraintType type,
    wl_client* client,
    wl_resource* resource,
    u32 id,
    wl_resource* wl_surface,
    wl_resource* wl_pointer,
    wl_resource* wl_region,
    u32 _lifetime)
{
    auto* surface = way_get_userdata<WaySurface>(wl_surface);
    auto* region = way_get_userdata<WayRegion>(wl_region);
    auto lifetime = zwp_pointer_constraints_v1_lifetime(_lifetime);

    log_debug("New pointer constraint(type = {}, lifetime = {})", type, lifetime);

    auto constraint = ref_create<WayPointerConstraint>();

    if (type == WmPointerConstraintType::locked) {
        constraint->resource = way_resource_create_refcounted(zwp_locked_pointer_v1, client, resource, id, constraint.get());
    } else {
        constraint->resource = way_resource_create_refcounted(zwp_confined_pointer_v1, client, resource, id, constraint.get());
    }

    // TODO: We shouldn't depend on a toplevel here
    auto* root = [](this auto&& self, WaySurface* surface) -> WaySurface* {
        return surface->parent ? self(surface->parent.get()) : surface;
    }(surface);
    debug_assert(root->toplevel, "TODO");

    constraint->constraint = wm_constrain_pointer(
        root->toplevel->window.get(),
        surface->scene.input_region.get(),
        region
            ? region->region
            : Region<f32>{aabb_make_infinite<f32>()},
        type);

    way_surface_addon_register(surface, constraint.get());
}

static
void lock_pointer(auto ...args)
{
    constrain_pointer(WmPointerConstraintType::locked, args...);
}

static
void confine_pointer(auto ...args)
{
    constrain_pointer(WmPointerConstraintType::confined, args...);
}

WAY_INTERFACE(zwp_pointer_constraints_v1) = {
    .destroy = way_simple_destroy,
    .lock_pointer = lock_pointer,
    .confine_pointer = confine_pointer,
};

WAY_BIND_GLOBAL(zwp_pointer_constraints_v1, bind)
{
    way_resource_create_unsafe(zwp_pointer_constraints_v1, bind.client, bind.version, bind.id, bind.server);
}

// -----------------------------------------------------------------------------

static
void set_region(wl_client* client, wl_resource* resource, wl_resource* wl_region)
{
    auto* constraint = way_get_userdata<WayPointerConstraint>(resource);
    auto* region = way_get_userdata<WayRegion>(wl_region);

    // TODO: Double buffer
    wm_pointer_constraint_set_region(constraint->constraint.get(), region->region);
}

WAY_INTERFACE(zwp_locked_pointer_v1) = {
    .destroy = way_simple_destroy,
    WAY_STUB(set_cursor_position_hint),
    .set_region = set_region,
};

WAY_INTERFACE(zwp_confined_pointer_v1) = {
    .destroy = way_simple_destroy,
    .set_region = set_region,
};

// -----------------------------------------------------------------------------

void on_active(WayPointerConstraint* constraint, bool active)
{
    log_warn("constraint {} active = {}", (void*)constraint, active);

    if (!constraint->resource) return;

    if (wl_resource_get_interface(constraint->resource) == &zwp_locked_pointer_v1_interface) {
        if (active) {
            way_send<zwp_locked_pointer_v1_send_locked>(  constraint->resource);
        } else {
            way_send<zwp_locked_pointer_v1_send_unlocked>(constraint->resource);
        }
    } else if (wl_resource_get_interface(constraint->resource) == &zwp_confined_pointer_v1_interface) {
        if (active) {
            way_send<zwp_confined_pointer_v1_send_confined>(  constraint->resource);
        } else {
            way_send<zwp_confined_pointer_v1_send_unconfined>(constraint->resource);
        }
    }
}

void way_pointer_constraint_on_active(WayClient* client, WmPointerConstraint* wm_pc, bool active)
{
    log_warn("way_pointer_constraint_on_active(client = {}, wmpc = {}, active = {})", (void*)client, (void*)wm_pc, active);

    for (auto* surface : client->surfaces) {
        if (surface->role != WaySurfaceRole::xdg_toplevel) continue;
        for (auto* addon : surface->addons) {
            if (auto* pc = dynamic_cast<WayPointerConstraint*>(addon)) {
                on_active(pc, active);
                break;
            }
        }
    }
}
