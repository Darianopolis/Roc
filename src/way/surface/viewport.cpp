#include "surface.hpp"

#include "../buffer/buffer.hpp"

static
void get_viewport(wl_client* client, wl_resource* resource, u32 id, wl_resource* surface)
{
    way_resource_create_refcounted(wp_viewport, client, resource, id, way_get_userdata<WaySurface>(surface));
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
    auto* surface = way_get_userdata<WaySurface>(resource);

    surface->pending->buffer_destination = {width, height};
    surface->pending->set |= WaySurfaceStateComponent::buffer_destination;
}

static
void set_source(wl_client* client, wl_resource* resource, wl_fixed_t x, wl_fixed_t y, wl_fixed_t width, wl_fixed_t height)
{
    auto* surface = way_get_userdata<WaySurface>(resource);

    auto src = rect_cast<f32>(rect2f64{
        {wl_fixed_to_double(x),     wl_fixed_to_double(y)},
        {wl_fixed_to_double(width), wl_fixed_to_double(height)},
        xywh,
    });

    surface->pending->buffer_source = src;
    surface->pending->set |= WaySurfaceStateComponent::buffer_source;
}

WAY_INTERFACE(wp_viewport) = {
    .destroy = way_simple_destroy,
    .set_source = set_source,
    .set_destination = set_destination,
};

// -----------------------------------------------------------------------------

void way_viewport_apply(WaySurface* surface, WaySurfaceState& from)
{
    auto& to = surface->current;

    if (from.set.contains(WaySurfaceStateComponent::buffer_source)) {
        if (from.buffer_source == rect2f32{{-1, -1}, {-1, -1}, xywh}) {
            to.set -= WaySurfaceStateComponent::buffer_source;
        } else {
            to.set |= WaySurfaceStateComponent::buffer_source;
            to.buffer_source = from.buffer_source;
        }
    }

    if (from.set.contains(WaySurfaceStateComponent::buffer_destination)) {
        if (from.buffer_destination == vec2i32{-1, -1}) {
            to.set -= WaySurfaceStateComponent::buffer_destination;
        } else {
            to.set |= WaySurfaceStateComponent::buffer_destination;
            to.buffer_destination = from.buffer_destination;
        }
    }

    auto* buffer = to.buffer.get();

    // Source

    if (buffer && to.set.contains(WaySurfaceStateComponent::buffer_source)) {
        auto src = to.buffer_source;
        src.origin /= vec_cast<f32>(buffer->extent);
        src.extent /= vec_cast<f32>(buffer->extent);
        scene_texture_set_src(surface->scene.texture.get(), src);

    } else if (!to.set.contains(WaySurfaceStateComponent::buffer_source)) {
        scene_texture_set_src(surface->scene.texture.get(), {{}, {1, 1}, xywh});
    }

    // Destination

    if (to.set.contains(WaySurfaceStateComponent::buffer_destination)) {
        scene_texture_set_dst(surface->scene.texture.get(), {{}, vec_cast<f32>(to.buffer_destination), xywh});

    } else if (to.set.contains(WaySurfaceStateComponent::buffer_source)) {
        scene_texture_set_dst(surface->scene.texture.get(), {{}, to.buffer_source.extent, xywh});

    } else if (buffer) {
        // Use buffer extent if destination and source have not been set before
        scene_texture_set_dst(surface->scene.texture.get(), {{}, vec_cast<f32>(buffer->extent), xywh});
    }
}
