#include "util.hpp"

#include "client.hpp"
#include "server.hpp"

auto way_resource_create(wl_client* client, const wl_interface* interface, i32 version, i32 id, const void* impl, WayUserdata data, bool refcount) -> wl_resource*
{
    debug_assert(data.data || !refcount);

#if WAY_CHECKED_USERDATA
    if (data.data) {
        auto* server = way_client_from(client)->server;
        way_userdata_register(server, {data.data, data.type});
    }
#endif

    auto resource = wl_resource_create(client, interface, version, id);
    if (refcount) {
        object_ref(data.data);
        wl_resource_set_implementation(resource, impl, data.data, [](wl_resource* resource) {
            object_unref(wl_resource_get_user_data(resource));
        });
    } else {
        wl_resource_set_implementation(resource, impl, data.data, nullptr);
    }
    return resource;
}

void way_simple_destroy(wl_client* client, wl_resource* resource)
{
    wl_resource_destroy(resource);
}

auto way_bind_data_from(wl_client* client, void* data, u32 version, u32 id) -> WayBindGlobalData
{
    return {way_client_from(client)->server, client, data, version, id};
}

#if WAY_CHECKED_USERDATA
void way_userdata_register(WayServer* server, WayUserdata data)
{
    server->userdata_types[data.data] = {
        .type = data.type,
        .version = allocation_get_version(allocation_from(data.data)),
    };
}

void way_userdata_check(WayServer* server, void* data, const std::type_info& type)
{
    auto iter = server->userdata_types.find(data);
    if (iter == server->userdata_types.end()) [[unlikely]] {
        debug_assert_fail(
            std::format("way_check_type({})", data),
            std::format("expected {}, but no userdata was registered at this location", type.name()));
    }
    if (iter->second.version != allocation_get_version(allocation_from(data))) [[unlikely]] {
        debug_assert_fail(
            std::format("way_check_type({})", data),
            std::format("expected {}, but userdata has been destroyed", type.name()));
    }
    if (iter->second.type != &type) [[unlikely]] {
        debug_assert_fail(
            std::format("way_check_type({})", data),
            std::format("expected {}, but found {}", type.name(), iter->second.type->name()));
    }
}

void way_userdata_check(wl_resource* resource, const std::type_info& type)
{
    auto* client = way_client_from(wl_resource_get_client(resource));
    way_userdata_check(client->server, wl_resource_get_user_data(resource), type);
}
#endif
