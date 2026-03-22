#include "internal.hpp"

void way_output_init(way_server* server)
{
    way_global(server, wl_output);
}

WAY_INTERFACE(wl_output) = {
    .release = way_simple_destroy,
};

WAY_BIND_GLOBAL(wl_output, bind)
{
    auto* server = way_get_userdata<way_server>(bind.data);
    auto resource = way_resource_create_unsafe(wl_output, bind.client, bind.version, bind.id, server);

    // TODO: This is just a temporary output to satisfy clients that need (but
    //       really shouldn't care about) a wl_output to function properly.

    static constexpr vec2i32 size = {1920, 1080};

    way_send(server, wl_output_send_geometry, resource,
        size.x, size.y,
        0, 0,
        WL_OUTPUT_SUBPIXEL_HORIZONTAL_RGB,
        "unknown",
        "unknown",
        WL_OUTPUT_TRANSFORM_NORMAL);

    way_send(server, wl_output_send_mode, resource,
        WL_OUTPUT_MODE_CURRENT | WL_OUTPUT_MODE_PREFERRED,
        size.x, size.y,
        0);

    if (bind.version >= WL_OUTPUT_SCALE_SINCE_VERSION) {
        way_send(server, wl_output_send_scale, resource, 1);
    }

    if (bind.version >= WL_OUTPUT_NAME_SINCE_VERSION) {
        way_send(server, wl_output_send_name, resource, "ROC-1");
    }

    if (bind.version >= WL_OUTPUT_DESCRIPTION_SINCE_VERSION) {
        way_send(server, wl_output_send_description, resource, "unknown");
    }

    if (bind.version >= WL_OUTPUT_DONE_SINCE_VERSION) {
        way_send(server, wl_output_send_done, resource);
    }
}
