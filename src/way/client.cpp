#include "internal.hpp"

static
auto find_surface(way_client* client, scene_window* window) -> way_surface*
{
    for (auto* surface : client->surfaces) {
        if (surface->toplevel.window.get() == window) {
            return surface;
        }
    }

    return nullptr;
}

static
void handle_event(way_client* client, scene_event* event)
{
    switch (event->type) {
        break;case scene_event_type::window_reposition: {
            auto* surface = find_surface(client, event->window.window);
            way_toplevel_on_reposition(surface, event->window.reposition.frame, event->window.reposition.gravity);
        }
        break;case scene_event_type::output_frame: {
            for (auto* surface : client->surfaces) {
                way_surface_on_redraw(surface);
            }
        }

        break;case scene_event_type::keyboard_enter:    way_seat_on_keyboard_enter(client, event);
        break;case scene_event_type::keyboard_leave:    way_seat_on_keyboard_leave(client, event);
        break;case scene_event_type::keyboard_key:      way_seat_on_key(           client, event);
        break;case scene_event_type::keyboard_modifier: way_seat_on_modifier(      client, event);

        break;case scene_event_type::pointer_enter:     way_seat_on_pointer_enter( client, event);
        break;case scene_event_type::pointer_leave:     way_seat_on_pointer_leave( client, event);
        break;case scene_event_type::pointer_motion:    way_seat_on_motion(        client, event);
        break;case scene_event_type::pointer_button:    way_seat_on_button(        client, event);
        break;case scene_event_type::pointer_scroll:    way_seat_on_scroll(        client, event);

        break;case scene_event_type::output_added:
              case scene_event_type::output_removed:
              case scene_event_type::output_configured:
              case scene_event_type::output_layout:
              case scene_event_type::output_frame_request:
            ;

        break;case scene_event_type::hotkey:
            ;

        break;case scene_event_type::selection:
            way_data_offer_selection(client);
    }
}

// -----------------------------------------------------------------------------

void way_on_client_create(wl_listener* listener, void* data)
{
    auto* server = way_get_userdata<way_server>(listener);
    auto* wl_client = static_cast<struct wl_client*>(data);

    auto client = core::create<way_client>();
    client->server = server;
    client->wl_client = wl_client;

    wl_client_set_user_data(wl_client, core::add_ref(client.get()), [](void* data) {
        core::remove_ref(way_get_userdata<way_client>(data));
    });

    client->scene = scene_client_create(server->scene);
    scene_client_set_event_handler(client->scene.get(), [client = client.get()](scene_event* event) {
        handle_event(client, event);
    });
}

way_client* way_client_from(way_server* server, const wl_client* client)
{
    // NOTE: `wl_client_get_user_data` does not actually require a non-const client.
    return way_get_userdata<way_client>(wl_client_get_user_data(const_cast<wl_client*>(client)));
}

auto way_client_is_behind(way_client* client) -> bool
{
    return poll(core::ptr_to(pollfd {
        .fd = wl_client_get_fd(client->wl_client),
        .events = POLLOUT,
    }), 1, 0) != 1;
}
