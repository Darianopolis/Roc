#include "client.hpp"

#include "surface/surface.hpp"
#include "shell/shell.hpp"
#include "seat/seat.hpp"

// -----------------------------------------------------------------------------

void way_on_client_create(wl_listener* listener, void* data)
{
    auto* server = way_get_userdata<WayServer>(listener);
    auto* wl_client = static_cast<struct wl_client*>(data);

    auto client = ref_create<WayClient>();
    client->server = server;
    client->wl_client = wl_client;

    wl_client_set_user_data(wl_client, object_ref(client.get()), [](void* data) {
        object_unref(static_cast<WayClient*>(data));
    });

    client->wm = wm_connect(server->wm);

    wm_listen(client->wm.get(), [client = client.get()](WmClient*, WmEvent* event) {
        switch (event->type) {
            break;case WmEventType::window_created:
                  case WmEventType::window_destroyed:
                  case WmEventType::window_mapped:
                  case WmEventType::window_unmapped:
                  case WmEventType::window_repositioned:
                  case WmEventType::window_reposition_requested:
                  case WmEventType::window_close_requested:
                way_handle_window_event(client, &event->window);

            break;case WmEventType::output_frame:
                for (auto* surface : client->surfaces) {
                    way_surface_on_frame(surface, event->output.output);
                }

            break;case WmEventType::seat_event:
                way_seat_handle_event(client, event->seat.event);

            break;case WmEventType::pointer_constraint_enabled:
                way_pointer_constraint_on_active(client, event->pointer_constraint.constraint, true);
            break;case WmEventType::pointer_constraint_disabled:
                way_pointer_constraint_on_active(client, event->pointer_constraint.constraint, false);

            break;default:
                ;
        }
    });
}

auto way_client_from(const wl_client* client) -> WayClient*
{
    // NOTE: `wl_client_get_user_data` does not actually require a non-const client.
    return static_cast<WayClient*>(wl_client_get_user_data(const_cast<wl_client*>(client)));
}

auto way_client_is_behind(WayClient* client) -> bool
{
    return poll(ptr_to(pollfd {
        .fd = wl_client_get_fd(client->wl_client),
        .events = POLLOUT,
    }), 1, 0) != 1;
}

void way_client_queue_flush(WayClient* client)
{
    // TODO: Queue to run later
    wl_client_flush(client->wl_client);
}
