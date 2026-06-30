#include "internal.hpp"

auto wm_connect(WmServer* server) -> Ref<WmClient>
{
    auto client = ref_create<WmClient>();
    client->server = server;
    client->seat_client = seat_connect(wm_get_seat_manager(server));
    server->clients.emplace_back(client.get());

    seat_listen(client->seat_client.get(), [client = client.get()](SeatEvent* event) {
        if (client->listener) {
            client->listener(client, ptr_to(WmEvent {
                .seat = {
                    .type = WmEventType::seat_event,
                    .event = event,
                }
            }));
        }
    });

    return client;
}

WmClient::~WmClient()
{
    for (auto* window : server->windows) {
        debug_assert(window->client != this);
    }

    std::erase(server->clients, this);
}

void wm_listen(WmClient* client, std::move_only_function<void(WmClient*, WmEvent*)> listener)
{
    client->listener = std::move(listener);
}

auto wm_get_seat_client(WmClient* client) -> SeatClient*
{
    return client->seat_client.get();
}

void wm_client_post_event(WmClient* client, WmEvent* event)
{
    if (client->listener) {
        client->listener(client, event);
    }
}

void wm_broadcast_event(WmServer* server, WmEvent* event)
{
    for (auto* client : server->clients) {
        wm_client_post_event(client, event);
    }
}
