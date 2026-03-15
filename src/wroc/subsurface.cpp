#include "protocol.hpp"

static
void wroc_wl_subcompositor_get_subsurface(wl_client* client, wl_resource* resource, u32 id, wl_resource* _surface, wl_resource* parent)
{
    auto* new_resource = wroc_resource_create(client, &wl_subsurface_interface, wl_resource_get_version(resource), id);
    auto* surface = wroc_get_userdata<wroc_surface>(_surface);
    auto* subsurface = core::create_unsafe<wroc_subsurface>();
    wroc_surface_put_addon(surface, subsurface);

    subsurface->resource = new_resource;
    subsurface->parent = wroc_get_userdata<wroc_surface>(parent);

    // Add subsurface to top of its parent's surface stack
    subsurface->parent->pending->surface_stack.emplace_back(subsurface->surface.get());
    subsurface->parent->pending->committed |= wroc_surface_committed_state::surface_stack;

    wroc_resource_set_implementation_refcounted(new_resource, &wroc_wl_subsurface_impl, subsurface);
}

const struct wl_subcompositor_interface wroc_wl_subcompositor_impl = {
    .destroy        = wroc_simple_resource_destroy_callback,
    .get_subsurface = wroc_wl_subcompositor_get_subsurface,
};

void wroc_wl_subcompositor_bind_global(wl_client* client, void* data, u32 version, u32 id)
{
    auto* new_resource = wroc_resource_create(client, &wl_subcompositor_interface, version, id);
    wroc_resource_set_implementation(new_resource, &wroc_wl_subcompositor_impl, nullptr);
}

// -----------------------------------------------------------------------------

static
auto find_in_stack(auto& stack, wroc_surface* surface)
{
    return std::ranges::find_if(stack, [&](const auto& layer) {
        return layer.surface.get() == surface;
    });
}

vec2i32 wroc_subsurface::position() const
{
    return find_in_stack(parent->pending->surface_stack, surface.get())->position;
}

static
void wroc_wl_subsurface_set_position(wl_client* client, wl_resource* resource, i32 x, i32 y)
{
    auto* subsurface = wroc_get_userdata<wroc_subsurface>(resource);
    auto* parent = subsurface->parent.get();

    auto& surface_stack = parent->pending->surface_stack;

    auto cur = find_in_stack(surface_stack, subsurface->surface.get());

    cur->position = {x, y};

    parent->pending->committed |= wroc_surface_committed_state::surface_stack;
}

static
void wroc_wl_subsurface_place(wl_resource* resource, wl_resource* _sibling, bool above)
{
    auto subsurface = wroc_get_userdata<wroc_subsurface>(resource);
    auto parent = subsurface->parent.get();
    auto sibling = wroc_get_userdata<wroc_surface>(_sibling);

    auto& surface_stack = parent->pending->surface_stack;

    auto cur = find_in_stack(surface_stack, subsurface->surface.get());
    auto sib = find_in_stack(surface_stack, sibling);

    if (cur == surface_stack.end()) return wroc_post_error(subsurface->resource, WL_SUBSURFACE_ERROR_BAD_SURFACE, "Compositor error: surface not in stack!");
    if (sib == surface_stack.end()) return wroc_post_error(subsurface->resource, WL_SUBSURFACE_ERROR_BAD_SURFACE, "Sibling not found in stack");
    if (cur == sib)                 return wroc_post_error(subsurface->resource, WL_SUBSURFACE_ERROR_BAD_SURFACE, "Passed self as sibling");

    if (cur > sib) std::rotate(sib + i32(above), cur, cur + 1);
    else           std::rotate(cur, cur + 1, sib + i32(above));

    parent->pending->committed |= wroc_surface_committed_state::surface_stack;
}

static
void wroc_wl_subsurface_place_above(wl_client* client, wl_resource* resource, wl_resource* sibling)
{
    wroc_wl_subsurface_place(resource, sibling, true);
}

static
void wroc_wl_subsurface_place_below(wl_client* client, wl_resource* resource, wl_resource* sibling)
{
    wroc_wl_subsurface_place(resource, sibling, false);
}

static
void wroc_wl_subsurface_set_sync(wl_client* client, wl_resource* resource)
{
    auto* subsurface = wroc_get_userdata<wroc_subsurface>(resource);
    if (!subsurface->synchronized) log_debug("Subsurface mode set to: synchronized");
    subsurface->synchronized = true;
}

static
void wroc_wl_subsurface_set_desync(wl_client* client, wl_resource* resource)
{
    auto* subsurface = wroc_get_userdata<wroc_subsurface>(resource);
    if (subsurface->synchronized) log_debug("Subsurface mode set to: desynchronized");
    subsurface->synchronized = false;
}

wroc_surface* wroc_subsurface_get_root_surface(wroc_subsurface* subsurface)
{
    auto* parent = subsurface->parent.get();
    if (auto* parent_ss = wroc_surface_get_addon<wroc_subsurface>(parent)) {
        return wroc_subsurface_get_root_surface(parent_ss);
    } else {
        return parent;
    }
}

const struct wl_subsurface_interface wroc_wl_subsurface_impl = {
    .destroy      = wroc_simple_resource_destroy_callback,
    .set_position = wroc_wl_subsurface_set_position,
    .place_above  = wroc_wl_subsurface_place_above,
    .place_below  = wroc_wl_subsurface_place_below,
    .set_sync     = wroc_wl_subsurface_set_sync,
    .set_desync   = wroc_wl_subsurface_set_desync,
};

void wroc_subsurface::commit(wroc_commit_id id)
{
    if (synchronized) {
        if (parent) {
            surface->pending->parent_commit = parent->committed + 1;
            surface->pending->committed |= wroc_surface_committed_state::parent_commit;
#if WROC_NOISY_SUBSURFACES
            log_debug("Synchronized subsurface committed, waiting for parent commit: {}", surface->pending->parent_commit);
#endif
        } else {
            log_error("Synchronized subsurface committed, but parent surface is dead!");
        }
    } else if (last_synchronized) {
        // Disabling synchronization should remove parent commit requirement from all previously unapplied synchronized state
        log_warn("Synchronization disabled for subsurface, removing requirement for all currently un-applied state packets");
        for (auto& packet : surface->cached) {
            if (packet.id) {
                packet.state.committed -= wroc_surface_committed_state::parent_commit;
            }
        }
    }
    last_synchronized = synchronized;
}
