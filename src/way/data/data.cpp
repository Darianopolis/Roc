#include "data.hpp"

#include "../seat/seat.hpp"
#include "../surface/surface.hpp"
#include "../client.hpp"

static
void create_data_source(wl_client* wl_client, wl_resource* resource, u32 id)
{
    auto* client = way_client_from(wl_client);

    auto source = ref_create<WayDataSource>();
    source->client = client;

    source->resource = way_resource_create_refcounted(wl_data_source, wl_client, resource, id, source.get());

    log_error("WayDataSource created {}", (void*)source.get());
}

void WayDataSource::on_cancel()
{
    way_send(wl_data_source, cancelled, resource);
}

void WayDataSource::on_send(const char* mime, fd_t fd)
{
    way_send(wl_data_source, send, resource, mime, fd);
}

WayDataSource::~WayDataSource()
{
    log_error("WayDataSource destroyed {}", (void*)this);
}

static
void get_data_device(wl_client* wl_client, wl_resource* resource, u32 id, wl_resource* wl_seat)
{
    auto* client_seat = way_get_userdata<WayClientSeat>(wl_seat);

    client_seat->data_devices.emplace_back(way_resource_create_refcounted(wl_data_device, wl_client, resource, id, client_seat));
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
void receive(wl_client* client, wl_resource* resource, const char* mime_type, fd_t fd)
{
    auto write = Fd(fd);

    auto* offer = way_get_userdata<WayDataOffer>(resource);
    seat_data_source_receive(offer->source.get(), mime_type, write.get());
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
    seat_data_source_offer(source, mime_type);
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
    log_error("TODO - wl_data_device{{{}}}::start_drag", (void*)data_device);
    if (data_source) {
        log_error("     - cancelling drag for wl_data_source{{{}}}", (void*)data_source);
        way_send(wl_data_source, cancelled, data_source);
    }
}

static
void set_selection(wl_client* wl_client, wl_resource* wl_data_device, wl_resource* wl_data_source, u32 serial)
{
    auto* client_seat = way_get_userdata<WayClientSeat>(wl_data_device);
    auto* source = way_get_userdata<WayDataSource>(wl_data_source);
    seat_set_selection(client_seat->seat->seat, source);
}

WAY_INTERFACE(wl_data_device) = {
    .start_drag = start_drag,
    .set_selection = set_selection,
    .release = way_simple_destroy,
};

// -----------------------------------------------------------------------------

static
auto make_offer(WayClientSeat* client_seat, wl_resource* wl_data_device, SeatDataSource* source) -> Ref<WayDataOffer>
{
    auto offer = ref_create<WayDataOffer>();
    offer->client_seat = client_seat;
    offer->source = source;

    offer->resource = way_resource_create_refcounted(wl_data_offer, client_seat->client->wl_client, wl_data_device, 0, offer.get());

    way_send(wl_data_device, data_offer, wl_data_device, offer->resource);
    for (auto& mime : seat_data_source_get_offered(offer->source.get())) {
        way_send(wl_data_offer, offer, offer->resource, mime.c_str());
    }

    if (wl_resource_get_version(offer->resource) >= WL_DATA_OFFER_SOURCE_ACTIONS_SINCE_VERSION) {
        way_send(wl_data_offer, source_actions, offer->resource, 0);
    }

    return offer;
}

void way_data_offer_selection(WayClientSeat* client_seat)
{
    auto* seat = client_seat->seat;
    auto* source = seat_get_selection(seat->seat);

    if (!source) return;
    if (!seat->focus.keyboard || !seat->focus.keyboard->client) return;

    for (auto* wl_data_device : client_seat->data_devices) {
        auto offer = make_offer(client_seat, wl_data_device, source);
        way_send(wl_data_device, selection, wl_data_device, offer->resource);
    }
}
