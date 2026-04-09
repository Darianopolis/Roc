#include "client.hpp"

#include "surface/surface.hpp"
#include "shell/shell.hpp"
#include "data/data.hpp"
#include "seat/seat.hpp"

static
auto get_seat_client(WayClient* client, SceneSeat* seat) -> WaySeatClient*
{
    for (auto* seat_client : client->seat_clients) {
        if (seat_client->seat->scene == seat) {
            return seat_client;
        }
    }
    return nullptr;
}

static
void handle_keyboard_event(WayClient* client, SceneEvent* event, auto&& fn)
{
    if (auto* seat_client = get_seat_client(client, scene_input_device_get_seat(scene_keyboard_get_base(event->keyboard.keyboard)))) {
        fn(seat_client, event);
    }
}

static
void handle_pointer_event(WayClient* client, SceneEvent* event, auto&& fn)
{
    if (auto* seat_client = get_seat_client(client, scene_input_device_get_seat(scene_pointer_get_base(event->pointer.pointer)))) {
        fn(seat_client, event);
    }
}

static
void handle_event(WayClient* client, SceneEvent* event)
{
    switch (event->type) {
        break;case SceneEventType::seat_add:
              case SceneEventType::seat_configure:
              case SceneEventType::seat_remove:
            ;

        break;case SceneEventType::keyboard_enter:    handle_pointer_event(client, event, way_seat_on_keyboard_enter);
        break;case SceneEventType::keyboard_leave:    handle_pointer_event(client, event, way_seat_on_keyboard_leave);
        break;case SceneEventType::keyboard_key:      handle_pointer_event(client, event, way_seat_on_key);
        break;case SceneEventType::keyboard_modifier: handle_pointer_event(client, event, way_seat_on_modifier);

        break;case SceneEventType::pointer_enter:  handle_pointer_event(client, event, way_seat_on_pointer_enter);
        break;case SceneEventType::pointer_leave:  handle_pointer_event(client, event, way_seat_on_pointer_leave);
        break;case SceneEventType::pointer_motion: handle_pointer_event(client, event, way_seat_on_motion);
        break;case SceneEventType::pointer_button: handle_pointer_event(client, event, way_seat_on_button);
        break;case SceneEventType::pointer_scroll: handle_pointer_event(client, event, way_seat_on_scroll);

        break;case SceneEventType::selection:
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

    client->scene = scene_client_create(wm_get_scene(server->wm));
    scene_client_set_event_handler(client->scene.get(), [client = client.get()](SceneEvent* event) {
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
