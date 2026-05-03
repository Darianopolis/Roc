#include "seat.hpp"
#include "../server.hpp"

static
void get_relative_pointer(wl_client* wl_client, wl_resource* resource, u32 id, wl_resource* pointer)
{
    auto* client = way_get_userdata<WayClientSeat>(pointer);

    client->relative_pointers.emplace_back(way_resource_create_unsafe(zwp_relative_pointer_v1, wl_client, resource, id, client));
}

WAY_INTERFACE(zwp_relative_pointer_manager_v1) = {
    .destroy = way_simple_destroy,
    .get_relative_pointer = get_relative_pointer,
};

WAY_BIND_GLOBAL(zwp_relative_pointer_manager_v1, bind)
{
    way_resource_create_unsafe(zwp_relative_pointer_manager_v1, bind.client, bind.version, bind.id, bind.server);
}

// -----------------------------------------------------------------------------

WAY_INTERFACE(zwp_relative_pointer_v1) = {
    .destroy = way_simple_destroy,
};
