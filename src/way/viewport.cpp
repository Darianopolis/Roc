#include "internal.hpp"

static
void get_viewport(wl_client* client, wl_resource* resource, u32 id, wl_resource* surface)
{
    way_resource_create_refcounted(wp_viewport, client, resource, id, way_get_userdata<way_surface>(surface));
}

WAY_INTERFACE(wp_viewporter) = {
    .destroy = way_simple_destroy,
    .get_viewport = get_viewport,
};

WAY_BIND_GLOBAL(wp_viewporter, bind)
{
    way_resource_create_unsafe(wp_viewporter, bind.client, bind.version, bind.id, bind.server);
}

// -----------------------------------------------------------------------------

static
void set_destination(wl_client* client, wl_resource* resource, i32 width, i32 height)
{
    auto* surface = way_get_userdata<way_surface>(resource);
    surface->pending->buffer_destination = {width, height};
    surface->pending->set(way_surface_committed_state::buffer_destination);
}

static
void set_source(wl_client* client, wl_resource* resource, wl_fixed_t x, wl_fixed_t y, wl_fixed_t width, wl_fixed_t height)
{
    auto* surface = way_get_userdata<way_surface>(resource);

    rect2f32 src {
        {wl_fixed_to_double(x),     wl_fixed_to_double(y)},
        {wl_fixed_to_double(width), wl_fixed_to_double(height)},
        core::xywh,
    };

    if (src == rect2f32{{-1, -1}, {-1, -1}, core::xywh}) {
        surface->pending->unset(way_surface_committed_state::buffer_source);
    } else {
        surface->pending->buffer_source = src;
        surface->pending->set(way_surface_committed_state::buffer_source);
    }
}

WAY_INTERFACE(wp_viewport) = {
    .destroy = way_simple_destroy,
    .set_source = set_source,
    .set_destination = set_destination,
};

// -----------------------------------------------------------------------------

void way_viewport_apply(way_surface* surface, way_surface_state& from)
{
    auto& to = surface->current;

    WAY_ADDON_SIMPLE_STATE_APPLY(from, to, buffer_source,      buffer_source);
    WAY_ADDON_SIMPLE_STATE_APPLY(from, to, buffer_destination, buffer_destination);

    auto* buffer = to.buffer.get();

    // Source

    if (buffer && to.is_set(way_surface_committed_state::buffer_source)) {
        auto src = to.buffer_source;
        src.origin /= vec2f32(buffer->extent);
        src.extent /= vec2f32(buffer->extent);
        scene_texture_set_src(surface->scene.texture.get(), src);

    } else if (!to.is_set(way_surface_committed_state::buffer_source)) {
        scene_texture_set_src(surface->scene.texture.get(), {{}, {1, 1}, core::xywh});
    }

    // Destination

    if (to.is_set(way_surface_committed_state::buffer_destination)) {
        scene_texture_set_dst(surface->scene.texture.get(), {{}, to.buffer_destination, core::xywh});

    } else if (to.is_set(way_surface_committed_state::buffer_source)) {
        scene_texture_set_dst(surface->scene.texture.get(), {{}, to.buffer_source.extent, core::xywh});

    } else if (buffer) {
        // Use buffer extent if destination and source have not been set before
        scene_texture_set_dst(surface->scene.texture.get(), {{}, buffer->extent, core::xywh});
    }
}
