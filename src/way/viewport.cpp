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

WAY_BIND_GLOBAL(wp_viewporter)
{
    way_resource_create_unsafe(wp_viewporter, client, version, id, way_get_userdata<way_server>(data));
}

// -----------------------------------------------------------------------------

static
void set_destination(wl_client* client, wl_resource* resource, i32 width, i32 height)
{
    auto* surface = way_get_userdata<way_surface>(resource);
    surface->pending->buffer.destination = {width, height};
    surface->pending->set(way_surface_committed_state::buffer_destination);
}

static
void set_source(wl_client* client, wl_resource* resource, wl_fixed_t x, wl_fixed_t y, wl_fixed_t width, wl_fixed_t height)
{
    auto* surface = way_get_userdata<way_surface>(resource);

    rect2f32 src {
        {wl_fixed_to_double(x),     wl_fixed_to_double(y)},
        {wl_fixed_to_double(width), wl_fixed_to_double(height)},
        core_xywh,
    };

    if (src == rect2f32{{-1, -1}, {-1, -1}, core_xywh}) {
        surface->pending->unset(way_surface_committed_state::buffer_source);
    } else {
        surface->pending->buffer.source = src;
        surface->pending->set(way_surface_committed_state::buffer_source);
    }
}

WAY_INTERFACE(wp_viewport) = {
    .destroy = way_simple_destroy,
    .set_source = set_source,
    .set_destination = set_destination,
};

// -----------------------------------------------------------------------------

#define WAY_NOISY_VIEWPORT 0

void way_viewport_apply(way_surface* surface, way_surface_state& from)
{
    auto& to = surface->current;

    WAY_ADDON_SIMPLE_STATE_APPLY(from, to, buffer.source, buffer_source);

#define   SET(Enum) from.is_set(  way_surface_committed_state::Enum)
#define UNSET(Enum) from.is_unset(way_surface_committed_state::Enum)
#define   HAS(Enum)   to.is_set(  way_surface_committed_state::Enum)

#if WAY_NOISY_VIEWPORT
#define LOG(...) log_debug(__VA_ARGS__)
#else
#define LOG(...)
#endif

    auto* buffer = to.buffer.handle.get();
    if (!buffer) return;

    // Source

    if (SET(buffer_source) || (SET(buffer) && HAS(buffer_source))) {
        auto src = to.buffer.source;
        src.origin /= vec2f32(buffer->extent);
        src.extent /= vec2f32(buffer->extent);
        LOG("buffer.source SET {} -> {}", core_to_string(to.buffer.source), core_to_string(src));
        scene_texture_set_src(surface->scene.texture.get(), src);

    } else if (UNSET(buffer_source)) {
        LOG("buffer.source UNSET");
        scene_texture_set_src(surface->scene.texture.get(), {{}, {1, 1}, core_xywh});
    }

    // Destination

    if (SET(buffer_destination)) {
        LOG("buffer.destination SET {}", core_to_string(from.buffer.destination));
        scene_texture_set_dst(surface->scene.texture.get(), {{}, from.buffer.destination, core_xywh});

    } else if (!HAS(buffer_destination)) {
        if (SET(buffer_source)) {
            LOG("buffer.destination SET to buffer.source.extent {}", core_to_string(to.buffer.source.extent));
            scene_texture_set_dst(surface->scene.texture.get(), {{}, to.buffer.source.extent, core_xywh});

        } else if (SET(buffer) && !HAS(buffer_source)) {
            // Use buffer extent if destination and source have not been set before
            LOG("buffer.destination DEFAULT to buffer.extent {}", core_to_string(buffer->extent));
            scene_texture_set_dst(surface->scene.texture.get(), {{}, buffer->extent, core_xywh});
        }
    }

#undef SET
#undef UNSET
#undef HAS
#undef LOG
}
