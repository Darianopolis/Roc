#include "internal.hpp"

#include <core/process.hpp>

static
auto close_focused(WmServer* server, Seat* seat, SeatFocus* focus) -> SeatEventFilterResult
{
    auto mods = seat_get_modifiers(seat);
    if (!mods.contains(server->main_mod)) return {};

    WmWindow* window;
    if (focus && (window = wm_find_window_for(server, focus))) {
        wm_window_request_close(window);
    }
    return SeatEventFilterResult::capture;
}

static
auto filter_event(WmServer* server, SeatEvent* event) -> SeatEventFilterResult
{
    switch (event->type) {
        break;case SeatEventType::keyboard_key:
            if (!event->keyboard.key.pressed) return {};

            if (event->keyboard.key.code == KEY_Q) {
                return close_focused(server,
                    seat_keyboard_get_seat(event->keyboard.keyboard),
                    seat_keyboard_get_focus(event->keyboard.keyboard));
            }

            if (seat_keyboard_get_modifiers(event->keyboard.keyboard).contains(server->main_mod)) {
                switch (event->keyboard.key.code) {
                    break;case KEY_S:
                        wm_focus(server, nullptr);
                        return SeatEventFilterResult::capture;
                    break;case KEY_F: {
                        auto window = wm_find_window_for(server, seat_keyboard_get_focus(event->keyboard.keyboard));
                        if (!window && !server->windows.empty()) {
                            window = server->windows.back();
                        }
                        if (window) {
                            auto seat = seat_keyboard_get_seat(event->keyboard.keyboard);
                            auto pointer = seat_get_pointer(seat);
                            auto output = wm_find_output_at(server, seat_pointer_get_position(pointer)).output;
                            if (wm_window_get_fullscreen(window) == output) {
                                wm_window_set_fullscreen(window, nullptr);
                            } else {
                                wm_window_set_fullscreen(window, output);
                            }
                        }
                        return SeatEventFilterResult::capture;
                    }
                }
            }
        break;case SeatEventType::pointer_button:
            if (!event->pointer.button.pressed) return {};
            switch (event->pointer.button.code) {
                break;case BTN_MIDDLE:
                    return close_focused(server,
                        seat_pointer_get_seat(event->pointer.pointer),
                        seat_pointer_get_focus(event->pointer.pointer));
            }
        break;default:
            ;
    }

    return {};
}

void wm_init_hotkeys(WmServer* server)
{
    server->hotkeys.filter = seat_add_event_filter(wm_get_seat(server), [server](SeatEvent* event) {
        return filter_event(server, event);
    });
}
