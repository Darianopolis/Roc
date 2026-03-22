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
auto get_seat_client(way_client* client, scene_seat* seat) -> way_seat_client*
{
    for (auto* seat_client : client->seat_clients) {
        if (seat_client->seat->scene_seat == seat) {
            return seat_client;
        }
    }
    return nullptr;
}

static
void handle_keyboard_event(way_client* client, scene_event* event, auto&& fn)
{
    if (auto* seat_client = get_seat_client(client, scene_keyboard_get_seat(event->keyboard.keyboard))) {
        fn(seat_client, event);
    }
}

static
void handle_pointer_event(way_client* client, scene_event* event, auto&& fn)
{
    if (auto* seat_client = get_seat_client(client, scene_pointer_get_seat(event->pointer.pointer))) {
        fn(seat_client, event);
    }
}

static
void handle_event(way_client* client, scene_event* event)
{
    switch (event->type) {
        break;case scene_event_type::seat_add:
              case scene_event_type::seat_configure:
              case scene_event_type::seat_remove:

        break;case scene_event_type::window_reposition: {
            auto* surface = find_surface(client, event->window.window);
            way_toplevel_on_reposition(surface, event->window.reposition.frame, event->window.reposition.gravity);
        }
        break;case scene_event_type::window_close: {
            auto* surface = find_surface(client, event->window.window);
            way_toplevel_on_close(surface);
        }

        break;case scene_event_type::output_frame: {
            for (auto* surface : client->surfaces) {
                way_surface_on_redraw(surface);
            }
        }

        break;case scene_event_type::keyboard_enter:    handle_pointer_event(client, event, way_seat_on_keyboard_enter);
        break;case scene_event_type::keyboard_leave:    handle_pointer_event(client, event, way_seat_on_keyboard_leave);
        break;case scene_event_type::keyboard_key:      handle_pointer_event(client, event, way_seat_on_key);
        break;case scene_event_type::keyboard_modifier: handle_pointer_event(client, event, way_seat_on_modifier);

        break;case scene_event_type::pointer_enter:  handle_pointer_event(client, event, way_seat_on_pointer_enter);
        break;case scene_event_type::pointer_leave:  handle_pointer_event(client, event, way_seat_on_pointer_leave);
        break;case scene_event_type::pointer_motion: handle_pointer_event(client, event, way_seat_on_motion);
        break;case scene_event_type::pointer_button: handle_pointer_event(client, event, way_seat_on_button);
        break;case scene_event_type::pointer_scroll: handle_pointer_event(client, event, way_seat_on_scroll);

        break;case scene_event_type::output_added:
              case scene_event_type::output_removed:
              case scene_event_type::output_configured:
              case scene_event_type::output_layout:
              case scene_event_type::output_frame_request:
            ;

        break;case scene_event_type::hotkey:
            ;

        break;case scene_event_type::selection:
            if (auto* seat_client = get_seat_client(client, event->data.seat)) {
                way_data_offer_selection(seat_client);
            }
    }
}

// -----------------------------------------------------------------------------

void way_on_client_create(wl_listener* listener, void* data)
{
    auto* server = way_get_userdata<way_server>(listener);
    auto* wl_client = static_cast<struct wl_client*>(data);

    auto client = core_create<way_client>();
    client->server = server;
    client->wl_client = wl_client;

    wl_client_set_user_data(wl_client, core_object_add_ref(client.get()), [](void* data) {
        core_object_remove_ref(way_get_userdata<way_client>(data));
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
    return poll(ptr_to(pollfd {
        .fd = wl_client_get_fd(client->wl_client),
        .events = POLLOUT,
    }), 1, 0) != 1;
}
