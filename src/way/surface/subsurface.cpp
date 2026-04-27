#include "surface.hpp"

#include "../shell/shell.hpp"

static
void ensure_tree(WaySurface* surface)
{
    if (surface->tree) return;

    surface->tree = ref_create<WaySurfaceTree>();
    way_surface_addon_register(surface, surface->tree.get());
}

static
auto get_root(WaySurface* surface) -> WaySurface*
{
    return surface->parent ? get_root(surface->parent.get()) : surface;
}

static
void get_subsurface(wl_client* client, wl_resource* resource, u32 id, wl_resource* wl_surface, wl_resource* wl_parent)
{
    auto* surface = way_get_userdata<WaySurface>(wl_surface);
    surface->role = WaySurfaceRole::subsurface;

    auto subsurface = ref_create<WaySubsurface>();
    subsurface->resource = way_resource_create_refcounted(wl_subsurface, client, resource, id, subsurface.get());

    surface->subsurface = subsurface.get();
    way_surface_addon_register(surface, subsurface.get());

    auto* parent = way_get_userdata<WaySurface>(wl_parent);
    surface->parent = parent;

    seat_focus_set_parent(surface->scene.focus.get(), parent->scene.focus.get());

    // Subsurfaces start synchronized
    surface->subsurface->synchronized = true;

    // Place into parent's surface stack
    ensure_tree(parent);
    parent->tree->queue.pending->places.emplace_back(WaySurfaceTreePlace {
        .reference = nullptr,
        .surface = surface,
        .above = true,
    });
}

WAY_INTERFACE(wl_subcompositor) = {
    .destroy = way_simple_destroy,
    .get_subsurface = get_subsurface,
};

WAY_BIND_GLOBAL(wl_subcompositor, bind)
{
    way_resource_create_unsafe(wl_subcompositor, bind.client, bind.version, bind.id, way_get_userdata<WayServer>(bind.data));
}

// -----------------------------------------------------------------------------

static
void set_position(wl_client* client, wl_resource* resource, i32 x, i32 y)
{
    auto* surface = way_get_userdata<WaySubsurface>(resource)->surface;
    if (!surface->parent) return;
    surface->parent->tree->queue.pending->moves.emplace_back(WaySurfaceTreeMove {
        .surface = surface,
        .position = {x, y},
    });
}

template<bool above>
void place(wl_client* client, wl_resource* resource, wl_resource* sibling)
{
    auto* surface = way_get_userdata<WaySubsurface>(resource)->surface;
    if (!surface->parent) return;
    surface->parent->tree->queue.pending->places.emplace_back(WaySurfaceTreePlace {
        .reference = way_get_userdata<WaySurface>(sibling),
        .surface = surface,
        .above = above,
    });
}

static
auto is_synchronized(WaySurface* surface) -> bool
{
    if (surface->role != WaySurfaceRole::subsurface) return false;
    return surface->subsurface->synchronized || (surface->parent && is_synchronized(surface->parent.get()));
}

template<bool sync>
void set_sync(wl_client* client, wl_resource* resource)
{
    auto* subsurface = way_get_userdata<WaySubsurface>(resource);
    subsurface->synchronized = sync;
}

WAY_INTERFACE(wl_subsurface) = {
    .destroy = way_simple_destroy,
    .set_position = set_position,
    .place_above = place<true>,
    .place_below = place<false>,
    .set_sync   = set_sync<true>,
    .set_desync = set_sync<false>,
};

WaySubsurface::~WaySubsurface()
{
    if (surface) {
        surface->role = WaySurfaceRole::none;
        surface->subsurface = nullptr;
    }
}

void WaySubsurface::commit(WayCommitId id)
{
    if (is_synchronized(surface) && surface->parent) {
        queue.pending->parent = surface->parent.get();
        queue.pending->parent_commit = surface->parent->last_commit_id + 1;
        queue.commit(id);
    }
}

auto WaySubsurface::test(WayCommitId id) -> bool
{
    auto* from = queue.peek(id);
    if (!from) return true;

    if (surface->parent && surface->parent == from->parent.get()
            && surface->parent->current.commit < from->parent_commit) {
        return false;
    } else if (!surface->parent) {
        log_warn("parent_commit set but parent is gone, applying");
    } else if (surface->parent != from->parent.get()) {
        log_warn("synchronized subsurface parent has changed, applying");
    }

    return true;
}

void WaySubsurface::apply(WayCommitId id)
{
    queue.dequeue(id);
}

// -----------------------------------------------------------------------------

void WaySurfaceTree::commit(WayCommitId id)
{
    queue.commit(id);
}

void WaySurfaceTree::apply(WayCommitId id)
{
    auto from = queue.dequeue(id);
    if (!from) return;

    for (auto& move : from->moves) {
        scene_tree_set_translation(move.surface->scene.tree.get(), move.position);
    }
    from->moves.clear();

    for (auto& place : from->places) {
        auto* reference = place.reference ? place.reference->scene.tree.get() : nullptr;
        if (place.above) {
            scene_tree_place_above(
                surface->scene.tree.get(),
                reference,
                place.surface->scene.tree.get());
        } else {
            scene_tree_place_below(
                surface->scene.tree.get(),
                reference,
                place.surface->scene.tree.get());
        }
    }
    from->places.clear();
}
