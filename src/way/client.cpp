#include "client.hpp"

#include "surface/surface.hpp"
#include "shell/shell.hpp"
#include "data/data.hpp"
#include "seat/seat.hpp"

static
auto get_seat_client(WayClient* client, Seat* seat) -> WaySeatClient*
{
    for (auto* seat_client : client->seat_clients) {
        if (seat_client->seat->scene == seat) {
            return seat_client;
        }
    }
    return nullptr;
}

static
void handle_keyboard_event(WayClient* client, SeatEvent* event, auto&& fn)
{
    if (auto* seat_client = get_seat_client(client, seat_keyboard_get_seat(event->keyboard.keyboard))) {
        fn(seat_client, event);
    }
}

static
void handle_pointer_event(WayClient* client, SeatEvent* event, auto&& fn)
{
    if (auto* seat_client = get_seat_client(client, seat_pointer_get_seat(event->pointer.pointer))) {
        fn(seat_client, event);
    }
}

static
void handle_event(WayClient* client, SeatEvent* event)
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
            if (auto* seat_client = get_seat_client(client, event->data.seat)) {
                way_data_offer_selection(seat_client);
            }
    }
}

// -----------------------------------------------------------------------------

void way_on_client_create(wl_listener* listener, void* data)
{
    auto* server = way_get_userdata<WayServer>(listener);
    auto* wl_client = static_cast<struct wl_client*>(data);

    auto client = ref_create<WayClient>();
    client->server = server;
    client->wl_client = wl_client;

    server->client.list.emplace_back(client.get());

    wl_client_set_user_data(wl_client, object_add_ref(client.get()), [](void* data) {
        object_remove_ref(way_get_userdata<WayClient>(data));
    });

    client->scene = seat_client_create();
    seat_client_set_event_handler(client->scene.get(), [client = client.get()](SeatEvent* event) {
        handle_event(client, event);
    });
}

WayClient::~WayClient()
{
    std::erase(server->client.list, this);
}

WayClient* way_client_from(WayServer* server, const wl_client* client)
{
    // NOTE: `wl_client_get_user_data` does not actually require a non-const client.
    return way_get_userdata<WayClient>(wl_client_get_user_data(const_cast<wl_client*>(client)));
}

auto way_client_is_behind(WayClient* client) -> bool
{
    return poll(ptr_to(pollfd {
        .fd = wl_client_get_fd(client->wl_client),
        .events = POLLOUT,
    }), 1, 0) != 1;
}
