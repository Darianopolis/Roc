#include "data.hpp"

#include "../seat/seat.hpp"
#include "../surface/surface.hpp"
#include "../client.hpp"

static
void create_data_source(wl_client* wl_client, wl_resource* resource, u32 id)
{
    auto* server = way_get_userdata<WayServer>(resource);
    auto* client = way_client_from(server, wl_client);

    auto source = ref_create<WayDataSource>();
    source->client = client;
    source->resource = way_resource_create_refcounted(wl_data_source, wl_client, resource, id, source.get());
    source->source = scene_data_source_create(client->scene.get(), {
        .cancel = [source = source.get()] {
            way_send(source->client->server, wl_data_source_send_cancelled, source->resource);
        },
        .send = [source = source.get()](const char* mime, int fd) {
            way_send(source->client->server, wl_data_source_send_send, source->resource, mime, fd);
        }
    });
}

static
void get_data_device(wl_client* wl_client, wl_resource* resource, u32 id, wl_resource* wl_seat)
{
    auto* seat_client = way_get_userdata<WaySeatClient>(wl_seat);

    seat_client->data_devices.emplace_back(way_resource_create_refcounted(wl_data_device, wl_client, resource, id, seat_client));
}

WAY_INTERFACE(wl_data_device_manager) = {
    .create_data_source = create_data_source,
    .get_data_device = get_data_device,
};

WAY_BIND_GLOBAL(wl_data_device_manager, bind)
{
    way_resource_create_unsafe(wl_data_device_manager, bind.client, bind.version, bind.id, way_get_userdata<WayServer>(bind.data));
}

// -----------------------------------------------------------------------------

static
void receive(wl_client* client, wl_resource* resource, const char* mime_type, int fd)
{
    auto write = Fd(fd);

    auto* offer = way_get_userdata<WayDataOffer>(resource);
    scene_data_source_receive(offer->source.get(), mime_type, write.get());
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
    auto* source = way_get_userdata<WayDataSource>(resource);
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
    auto* client = way_get_userdata<WayClient>(data_device);
    log_error("TODO - wl_data_device{{{}}}::start_drag", (void*)data_device);
    if (data_source) {
        log_error("     - cancelling drag for wl_data_source{{{}}}", (void*)data_source);
        way_send(client->server, wl_data_source_send_cancelled, data_source);
    }
}

static
void set_selection(wl_client* wl_client, wl_resource* wl_data_device, wl_resource* wl_data_source, u32 serial)
{
    auto* seat_client = way_get_userdata<WaySeatClient>(wl_data_device);
    auto* source = way_get_userdata<WayDataSource>(wl_data_source);
    scene_seat_set_selection(seat_client->seat->scene, source->source.get());
}

WAY_INTERFACE(wl_data_device) = {
    .start_drag = start_drag,
    .set_selection = set_selection,
    .release = way_simple_destroy,
};

// -----------------------------------------------------------------------------

static
auto make_offer(WaySeatClient* seat_client, wl_resource* wl_data_device, SceneDataSource* source) -> Ref<WayDataOffer>
{
    auto* server = seat_client->seat->server;

    auto offer = ref_create<WayDataOffer>();
    offer->seat_client = seat_client;
    offer->source = source;

    offer->resource = way_resource_create_refcounted(wl_data_offer, seat_client->client->wl_client, wl_data_device, 0, offer.get());

    way_send(server, wl_data_device_send_data_offer, wl_data_device, offer->resource);
    for (auto& mime : scene_data_source_get_offered(offer->source.get())) {
        way_send(server, wl_data_offer_send_offer, offer->resource, mime.c_str());
    }

    if (wl_resource_get_version(offer->resource) >= WL_DATA_OFFER_SOURCE_ACTIONS_SINCE_VERSION) {
        way_send(server, wl_data_offer_send_source_actions, offer->resource, 0);
    }

    return offer;
}

void way_data_offer_selection(WaySeatClient* seat_client)
{
    auto* seat = seat_client->seat;
    auto* server = seat->server;
    auto* source = scene_seat_get_selection(seat->scene);

    if (!source) return;
    if (!seat->focus.keyboard || !seat->focus.keyboard->client) return;

    for (auto* wl_data_device : seat_client->data_devices) {
        auto offer = make_offer(seat_client, wl_data_device, source);
        way_send(server, wl_data_device_send_selection, wl_data_device, offer->resource);
    }
}
