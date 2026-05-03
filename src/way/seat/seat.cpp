#include "seat.hpp"

#include "../data/data.hpp"
#include "../surface/surface.hpp"
#include "../client.hpp"

// -----------------------------------------------------------------------------

static
void init_seat(WayServer* server, Seat* scene_seat)
{
    auto seat = ref_create<WaySeat>();
    seat->server = server;
    seat->seat = scene_seat;

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

static
auto find_client_seat(WayClient* client, Seat* seat) -> WayClientSeat*
{
    for (auto* cs : client->seats) {
        if (cs->seat->seat == seat) {
            return cs;
        }
    }
    return nullptr;
}

WAY_BIND_GLOBAL(wl_seat, bind)
{
    auto* seat = way_get_userdata<WaySeat>(bind.server, bind.data);
    auto* client = way_client_from(bind.client);

    Ref client_seat = find_client_seat(client, seat->seat);
    if (!client_seat) {
        client_seat = ref_create<WayClientSeat>();
        client_seat->client = client;
        client_seat->seat = seat;
        client->seats.emplace_back(client_seat.get());
    }

    auto* resource = way_resource_create_refcounted(wl_seat, bind.client, bind.version, bind.id, client_seat.get());

    way_send(wl_seat, capabilities, resource, WL_SEAT_CAPABILITY_KEYBOARD | WL_SEAT_CAPABILITY_POINTER);

    if (bind.version >= WL_SEAT_NAME_SINCE_VERSION) {
        way_send(wl_seat, name, resource, seat_get_name(seat->seat));
    }

    // TODO: Synchronize with current seat keyboard/pointer/data state
}

WayClientSeat::~WayClientSeat()
{
    std::erase(client->seats, this);
}

// ---------------------------------------------------------------------------------------

static
void handle_keyboard_event(WayClient* client, SeatEvent* event, auto&& fn)
{
    if (auto* client_seat = find_client_seat(client, seat_keyboard_get_seat(event->keyboard.keyboard))) {
        fn(client_seat, event);
    }
}

static
void handle_pointer_event(WayClient* client, SeatEvent* event, auto&& fn)
{
    if (auto* client_seat = find_client_seat(client, seat_pointer_get_seat(event->pointer.pointer))) {
        fn(client_seat, event);
    }
}


void way_seat_handle_event(WayClient* client, SeatEvent* event)
{
    switch (event->type) {
        break;case SeatEventType::keyboard_enter:    handle_pointer_event(client, event, way_seat_on_keyboard_enter);
        break;case SeatEventType::keyboard_leave:    handle_pointer_event(client, event, way_seat_on_keyboard_leave);
        break;case SeatEventType::keyboard_key:      handle_pointer_event(client, event, way_seat_on_key);
        break;case SeatEventType::keyboard_modifier: handle_pointer_event(client, event, way_seat_on_modifier);

        break;case SeatEventType::pointer_enter:  handle_pointer_event(client, event, way_seat_on_pointer_enter);
        break;case SeatEventType::pointer_leave:  handle_pointer_event(client, event, way_seat_on_pointer_leave);
        break;case SeatEventType::pointer_motion: handle_pointer_event(client, event, way_seat_on_motion);
        break;case SeatEventType::pointer_button: handle_pointer_event(client, event, way_seat_on_button);
        break;case SeatEventType::pointer_scroll: handle_pointer_event(client, event, way_seat_on_scroll);

        break;case SeatEventType::selection:
            if (auto* client_seat = find_client_seat(client, event->data.seat)) {
                way_data_offer_selection(client_seat);
            }
    }
}
