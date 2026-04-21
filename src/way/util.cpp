#include "util.hpp"

auto way_resource_create(wl_client* client, const wl_interface* interface, int version, int id, const void* impl, WayObject* data, bool refcount) -> wl_resource*
{
    auto resource = wl_resource_create(client, interface, version, id);
    if (refcount) {
        object_add_ref(data);
        wl_resource_set_implementation(resource, impl, data, [](wl_resource* resource) {
            object_remove_ref(wl_resource_get_user_data(resource));
        });
    } else {
        wl_resource_set_implementation(resource, impl, data, nullptr);
    }
    return resource;
}


void way_simple_destroy(wl_client* client, wl_resource* resource)
{
    wl_resource_destroy(resource);
}
