#include "internal.hpp"

static
void get_subsurface(wl_client* client, wl_resource* resource, u32 id, wl_resource* wl_surface, wl_resource* wl_parent)
{
    auto* surface = way_get_userdata<way_surface>(wl_surface);
    surface->role = way_surface_role::subsurface;
    surface->subsurface.resource = way_resource_create_refcounted(wl_subsurface, client, resource, id, surface);

    auto* parent = way_get_userdata<way_surface>(wl_parent);
    surface->parent = parent;

    // Subsurfaces start synchronized
    surface->subsurface.synchronized = true;

    // Place into parent's surface stack
    parent->queue.pending->subsurface.places.emplace_back(way_subsurface_place {
        .reference = nullptr,
        .subsurface = surface,
        .above = true,
    });
}

WAY_INTERFACE(wl_subcompositor) = {
    .destroy = way_simple_destroy,
    .get_subsurface = get_subsurface,
};

WAY_BIND_GLOBAL(wl_subcompositor, bind)
{
    way_resource_create_unsafe(wl_subcompositor, bind.client, bind.version, bind.id, way_get_userdata<way_server>(bind.data));
}

// -----------------------------------------------------------------------------

static
void set_position(wl_client* client, wl_resource* resource, i32 x, i32 y)
{
    auto* surface = way_get_userdata<way_surface>(resource);
    if (!surface->parent) return;
    surface->parent->queue.pending->subsurface.moves.emplace_back(way_subsurface_move {
        .subsurface = surface,
        .position = {x, y},
    });
}

template<bool above>
void place(wl_client* client, wl_resource* resource, wl_resource* sibling)
{
    auto* surface = way_get_userdata<way_surface>(resource);
    if (!surface->parent) return;
    surface->parent->queue.pending->subsurface.places.emplace_back(way_subsurface_place {
        .reference = way_get_userdata<way_surface>(sibling),
        .subsurface = surface,
        .above = above,
    });
}

static
bool is_synchronized(way_surface* surface)
{
    if (surface->role != way_surface_role::subsurface) return false;
    return surface->subsurface.synchronized || (surface->parent && is_synchronized(surface->parent.get()));
}

template<bool sync>
void set_sync(wl_client* client, wl_resource* resource)
{
    auto* surface = way_get_userdata<way_surface>(resource);
    surface->subsurface.synchronized = sync;
}

WAY_INTERFACE(wl_subsurface) = {
    .destroy = way_simple_destroy,
    .set_position = set_position,
    .place_above = place<true>,
    .place_below = place<false>,
    .set_sync   = set_sync<true>,
    .set_desync = set_sync<false>,
};

// -----------------------------------------------------------------------------

void way_subsurface_commit(way_surface* surface, way_surface_state& pending)
{
    if (is_synchronized(surface) && surface->parent) {
        pending.parent.commit = surface->parent->last_commit_id + 1;
        pending.set(way_surface_committed_state::parent_commit);
    }
}

void way_subsurface_apply(way_surface* surface, way_surface_state& from)
{
    // Arrange child subsurfaces

    for (auto& move : from.subsurface.moves) {
        scene_tree_set_translation(move.subsurface->scene.tree.get(), move.position);
    }
    from.subsurface.moves.clear();

    for (auto& place : from.subsurface.places) {
        auto* reference = place.reference ? place.reference->scene.tree.get() : nullptr;
        if (place.above) {
            scene_tree_place_above(
                surface->scene.tree.get(),
                reference,
                place.subsurface->scene.tree.get());
        } else {
            scene_tree_place_below(
                surface->scene.tree.get(),
                reference,
                place.subsurface->scene.tree.get());
        }
    }
    from.subsurface.places.clear();
}
