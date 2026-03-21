#include "internal.hpp"

static
void create_data_source(wl_client* client, wl_resource* resource, u32 id)
{
    auto* server = way_get_userdata<way_server>(resource);

    auto source = core_create<way_data_source>();
    source->client = way_client_from(server, client);
    source->resource = way_resource_create_refcounted(wl_data_source, client, resource, id, source.get());
    source->source = scene_data_source_create(source->client->scene.get(), {
        .cancel = [source = source.get()] {
            way_send(source->client->server, wl_data_source_send_cancelled, source->resource);
        },
        .send = [source = source.get()](const char* mime, int fd) {
            way_send(source->client->server, wl_data_source_send_send, source->resource, mime, fd);
        }
    });
}

static
void get_data_device(wl_client* wl_client, wl_resource* resource, u32 id, wl_resource* seat)
{
    auto* server = way_get_userdata<way_server>(resource);
    auto* client = way_client_from(server, wl_client);

    client->data_devices.emplace_back(way_resource_create_refcounted(wl_data_device, wl_client, resource, id, client));
}

WAY_INTERFACE(wl_data_device_manager) = {
    .create_data_source = create_data_source,
    .get_data_device = get_data_device,
};

WAY_BIND_GLOBAL(wl_data_device_manager, bind)
{
    way_resource_create_unsafe(wl_data_device_manager, bind.client, bind.version, bind.id, bind.server);
}

// -----------------------------------------------------------------------------

static
void receive(wl_client* client, wl_resource* resource, const char* mime_type, int fd)
{
    auto write = core_fd(fd);

    auto* offer = way_get_userdata<way_data_offer>(resource);
    scene_data_source_send(offer->source.get(), mime_type, write.get());
}

WAY_INTERFACE(wl_data_offer) = {
    WAY_STUB(accept),
    .receive = receive,
    .destroy = way_simple_destroy,
    WAY_STUB(finish),
    WAY_STUB(set_actions),
};

// -----------------------------------------------------------------------------

static
void offer(wl_client* client, wl_resource* resource, const char* mime_type)
{
    auto* source = way_get_userdata<way_data_source>(resource);
    scene_data_source_offer(source->source.get(), mime_type);
}

WAY_INTERFACE(wl_data_source) = {
    .offer = offer,
    .destroy = way_simple_destroy,
    WAY_STUB(set_actions),
};

// -----------------------------------------------------------------------------

static
void start_drag(
    wl_client*   wl_client,
    wl_resource* data_device,
    wl_resource* data_source,
    wl_resource* origin_surface,
    wl_resource* icon_surface,
    u32 serial)
{
    auto* client = way_get_userdata<way_client>(data_device);
    log_error("TODO - wl_data_device{{{}}}::start_drag", (void*)data_device);
    if (data_source) {
        log_error("     - cancelling drag for wl_data_source{{{}}}", (void*)data_source);
        way_send(client->server, wl_data_source_send_cancelled, data_source);
    }
}

static
void set_selection(wl_client* wl_client, wl_resource* resource, wl_resource* wl_data_source, u32 serial)
{
    auto* source = way_get_userdata<way_data_source>(wl_data_source);
    scene_set_selection(source->client->server->scene, source->source.get());
}

WAY_INTERFACE(wl_data_device) = {
    .start_drag = start_drag,
    .set_selection = set_selection,
    .release = way_simple_destroy,
};

// -----------------------------------------------------------------------------

static
auto make_offer(way_client* client, wl_resource* wl_data_device, scene_data_source* source) -> ref<way_data_offer>
{
    auto* server = client->server;

    auto offer = core_create<way_data_offer>();
    offer->client = client;
    offer->source = source;

    offer->resource = way_resource_create_refcounted(wl_data_offer, client->wl_client, wl_data_device, 0, offer.get());

    way_send(server, wl_data_device_send_data_offer, wl_data_device, offer->resource);
    for (auto& mime : scene_data_source_get_offered(offer->source.get())) {
        way_send(server, wl_data_offer_send_offer, offer->resource, mime.c_str());
    }

    if (wl_resource_get_version(offer->resource) >= WL_DATA_OFFER_SOURCE_ACTIONS_SINCE_VERSION) {
        way_send(server, wl_data_offer_send_source_actions, offer->resource, 0);
    }

    return offer;
}

void way_data_offer_selection(way_client* client)
{
    auto* server = client->server;
    auto* source = scene_get_selection(server->scene);

    if (!source) return;
    if (!server->focus.keyboard || !server->focus.keyboard->client) return;

    for (auto* wl_data_device : client->data_devices) {
        auto offer = make_offer(client, wl_data_device, source);
        way_send(server, wl_data_device_send_selection, wl_data_device, offer->resource);
    }
}
