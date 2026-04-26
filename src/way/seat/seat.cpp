#include "seat.hpp"

#include "../data/data.hpp"
#include "../surface/surface.hpp"
#include "../client.hpp"
#include "../shell/shell.hpp"

// -----------------------------------------------------------------------------

static
void init_seat(WayServer* server, Seat* scene_seat)
{
    auto seat = ref_create<WaySeat>();
    seat->server = server;
    seat->scene = scene_seat;

    server->seats.emplace_back(seat.get());

    way_seat_keyboard_init(seat.get());

    seat->global = way_global(server, wl_seat, seat.get());

    way_global(server, zwp_relative_pointer_manager_v1);
    way_global(server, zwp_pointer_constraints_v1);
}

WaySeat::~WaySeat()
{
    wl_global_remove(global);
}

void way_seat_init(WayServer* server)
{
    init_seat(server, wm_get_seat(server->wm));
}

// -----------------------------------------------------------------------------

WAY_INTERFACE(wl_seat) = {
    .get_pointer = way_seat_get_pointer,
    .get_keyboard = way_seat_get_keyboard,
    WAY_STUB(get_touch),
    .release = way_simple_destroy,
};

WAY_BIND_GLOBAL(wl_seat, bind)
{
    auto* seat = way_get_userdata<WaySeat>(bind.data);
    auto* client = way_client_from(bind.client);

    auto seat_client = ref_create<WaySeatClient>();
    seat_client->seat = seat;
    seat_client->client = client;

    client->seat_clients.emplace_back(seat_client.get());

    auto* resource = way_resource_create_refcounted(wl_seat, bind.client, bind.version, bind.id, seat_client.get());

    way_send(wl_seat_send_capabilities, resource, WL_SEAT_CAPABILITY_KEYBOARD | WL_SEAT_CAPABILITY_POINTER);

    if (bind.version >= WL_SEAT_NAME_SINCE_VERSION) {
        way_send(wl_seat_send_name, resource, "seat0");
    }

    // TODO: Synchronize with current seat keyboard/pointer/data state

    way_data_offer_selection(seat_client.get());
}

WaySeatClient::~WaySeatClient()
{
    std::erase(client->seat_clients, this);
}
