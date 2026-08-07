#include "buffer.hpp"

static
void get_tearing_control(wl_client* client, wl_resource* resource, u32 id, wl_resource* surface)
{
    way_resource_create_unsafe(wp_tearing_control_v1, client, resource, id, nullptr);
}

WAY_INTERFACE(wp_tearing_control_manager_v1) = {
    .destroy = way_simple_destroy,
    .get_tearing_control = get_tearing_control,
};

WAY_BIND_GLOBAL(wp_tearing_control_manager_v1, bind)
{
    way_resource_create_unsafe(wp_tearing_control_manager_v1, bind.client, bind.version, bind.id, nullptr);
}

static
void set_presentation_hint(wl_client* client, wl_resource* resoruce, u32 hint)
{
}

WAY_INTERFACE(wp_tearing_control_v1) = {
    .set_presentation_hint = set_presentation_hint,
    .destroy = way_simple_destroy,
};
