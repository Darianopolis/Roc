#include "protocol.hpp"

#include "core/math.hpp"

const u32 wroc_wp_viewporter_version = 1;

// -----------------------------------------------------------------------------

static
void wroc_wp_viewporter_get_viewport(wl_client* client, wl_resource* resource, u32 id, wl_resource* _surface)
{
    auto surface = wroc_get_userdata<wroc_surface>(_surface);
    log_debug("wp_viewporter::get_viewporter(surface = {})", (void*)surface);
    auto new_resource = wroc_resource_create(client, &wp_viewport_interface, wl_resource_get_version(resource), id);
    auto viewport = core::create_unsafe<wroc_viewport>();
    viewport->resource = resource;
    wroc_surface_put_addon(surface, viewport);
    wroc_resource_set_implementation_refcounted(new_resource, &wroc_wp_viewport_impl, viewport);
}

const struct wp_viewporter_interface wroc_wp_viewporter_impl
{
    .destroy = wroc_simple_resource_destroy_callback,
    .get_viewport = wroc_wp_viewporter_get_viewport,
};

void wroc_wp_viewporter_bind_global(wl_client* client, void* data, u32 version, u32 id)
{
    log_debug("wp_viewporter::bind_global()");
    auto new_resource = wroc_resource_create(client, &wp_viewporter_interface, version, id);
    wroc_resource_set_implementation(new_resource, &wroc_wp_viewporter_impl, nullptr);
}

// -----------------------------------------------------------------------------

static
void wroc_wp_viewport_set_source(wl_client* client, wl_resource* resource, wl_fixed_t x, wl_fixed_t y, wl_fixed_t width, wl_fixed_t height)
{
    auto* viewport = wroc_get_userdata<wroc_viewport>(resource);
    viewport->pending->committed |= wroc_viewport_committed_state::source;
    viewport->pending->source = {{wl_fixed_to_double(x), wl_fixed_to_double(y)}, {wl_fixed_to_double(width), wl_fixed_to_double(height)}, core::xywh};
}

static
void wroc_wp_viewport_set_destination(wl_client* client, wl_resource* resource, i32 width, i32 height)
{
    auto* viewport = wroc_get_userdata<wroc_viewport>(resource);
    viewport->pending->committed |= wroc_viewport_committed_state::destination;
    viewport->pending->destination = {width, height};
}

const struct wp_viewport_interface wroc_wp_viewport_impl
{
    .destroy = wroc_simple_resource_destroy_callback,
    .set_source = wroc_wp_viewport_set_source,
    .set_destination = wroc_wp_viewport_set_destination,
};

void wroc_viewport::commit(wroc_commit_id id)
{
    wroc_surface_state_queue_commit(this, id);
}

static
void apply_state(wroc_viewport* self, wroc_viewport_state& from, wroc_commit_id id)
{
    auto& to = self->current;

    if (from.committed.contains(wroc_viewport_committed_state::source)) {
        if (from.source == rect2f64{{-1, -1}, {-1, -1}, core::xywh}) {
            log_debug("wp_viewport source unset");
            to.committed -= wroc_viewport_committed_state::source;
        } else {
            log_debug("wp_viewport source = {}", core::to_string(from.source));
            to.source = from.source;
            to.committed |= wroc_viewport_committed_state::source;
        }
    }

    if (from.committed.contains(wroc_viewport_committed_state::destination)) {
        if (from.destination == vec2i32{-1, -1}) {
            log_debug("wp_viewport destination unset");
            to.committed -= wroc_viewport_committed_state::destination;
        } else {
            log_debug("wp_viewport destination = {}", core::to_string(from.destination));
            to.destination = from.destination;
            to.committed |= wroc_viewport_committed_state::destination;
        }
    }
}

void wroc_viewport::apply(wroc_commit_id id)
{
    wroc_surface_state_queue_apply(this, id, apply_state);
}
