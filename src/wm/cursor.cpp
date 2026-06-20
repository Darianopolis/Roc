#include "internal.hpp"

#include <core/log.hpp>

static
auto find_client(WmServer* server, SeatFocus* focus) -> WmClient*
{
    if (!focus) return nullptr;
    for (auto* client : server->clients) {
        if (client->seat_client.get() == focus->client) {
            return client;
        }
    }
    return nullptr;
}

static
void update_seat_cursor_visual(WmServer* server, Seat* seat)
{
    auto* pointer = seat_get_pointer(seat);

    switch (server->mode) {
        break;case WmInteractionMode::none:
            ;
        break;case WmInteractionMode::move:
            seat_pointer_set_xcursor(pointer, "move");
            return;
        break;case WmInteractionMode::size: {
            if (server->movesize.relative.x == 0) {
                seat_pointer_set_xcursor(pointer, "ns-resize");
            } else if (server->movesize.relative.y == 0) {
                seat_pointer_set_xcursor(pointer, "ew-resize");
            } else if (server->movesize.relative.x == server->movesize.relative.y) {
                seat_pointer_set_xcursor(pointer, "nwse-resize");
            } else {
                seat_pointer_set_xcursor(pointer, "nesw-resize");
            }
            return;
        }
        break;case WmInteractionMode::zone:
            if (server->zone.selecting) {
                seat_pointer_set_xcursor(pointer, "grabbing");
            } else {
                seat_pointer_set_xcursor(pointer, "grab");
            }
            return;
        break;case WmInteractionMode::focus_cycle:
            seat_pointer_set_xcursor(pointer, "default");
            return;
        break;case WmInteractionMode::selection:
            seat_pointer_set_xcursor(pointer, "crosshair");
            return;
    }
    if (server->mode != WmInteractionMode::none) {
        seat_pointer_set_xcursor(pointer, "default");
        return;
    }

    auto* client = find_client(server, seat_pointer_get_focus(pointer));
    if (!client) {
        seat_pointer_set_xcursor(pointer, "default");
        return;
    }

    std::visit(OverloadSet {
        [&](WmCursorUnset) {
            seat_pointer_set_xcursor(pointer, "default");
        },
        [&](WmCursorHidden) {
            if (find_client(server, seat_keyboard_get_focus(seat_get_keyboard(seat))) == client) {
                seat_pointer_set_cursor(pointer, nullptr);
            } else {
                log_warn("WmClient{{{}}} wanted to hide cursor but does not have keyboard focus", (void*)client);
                seat_pointer_set_xcursor(pointer, "default");
            }
        },
        [&](Weak<SceneNode> node) {
            if (node) {
                seat_pointer_set_cursor(pointer, node.get());
            } else {
                log_warn("WmClient{{{}}}'s cursor SceneNode has been destroyed, falling back to <default>", (void*)client);
                seat_pointer_set_xcursor(pointer, "default");
            }
        },
        [&](const std::string& semantic) {
            seat_pointer_set_xcursor(pointer, semantic.c_str());
        }
    }, client->cursor);
}

void wm_cursor_visual_update(WmServer* server)
{
    for (auto* seat : wm_get_seats(server)) {
        update_seat_cursor_visual(server, seat);
    }
}

void wm_cursor_init(WmServer* server)
{
    for (auto* seat : server->seats) {
        server->cursor_event_filter.emplace_back(seat_add_event_filter(seat, [server](SeatEvent* event) -> SeatEventFilterResult {
            if (       event->type == SeatEventType::keyboard_enter || event->type == SeatEventType::keyboard_leave
                    || event->type == SeatEventType::pointer_enter  || event->type == SeatEventType::pointer_leave) {
                wm_cursor_visual_update(server);
                exec_enqueue(server->exec, [server = Weak(server)] {
                    if (!server) return;
                    wm_cursor_visual_update(server.get());
                });
            }
            return SeatEventFilterResult::passthrough;
        }));
    }
    wm_cursor_visual_update(server);
}

void wm_set_cursor(WmClient* client, SceneNode* visual)
{
    auto old = client->cursor;
    if (visual) {
        client->cursor = Weak(visual);
    } else {
        client->cursor = WmCursorHidden {};
    }
    if (client->cursor != old) {
        wm_cursor_visual_update(client->server);
    }
}

void wm_set_xcursor(WmClient* client, const char* semantic)
{
    auto old = client->cursor;
    if (semantic) {
        client->cursor = std::string(semantic);
    } else {
        client->cursor = WmCursorHidden {};
    }
    if (client->cursor != old) {
        wm_cursor_visual_update(client->server);
    }
}
