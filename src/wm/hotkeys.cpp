#include "internal.hpp"

static
auto close_focused(WmServer* wm, Seat* seat, SeatFocus* focus) -> SeatEventFilterResult
{
    auto mods = seat_get_modifiers(seat);
    if (!mods.contains(wm->main_mod)) return {};

    WmWindow* window;
    if (focus && (window = wm_find_window_for(wm, focus))) {
        wm_window_request_close(window);
    }
    return SeatEventFilterResult::capture;
}

static
auto filter_event(WmServer* wm, SeatEvent* event) -> SeatEventFilterResult
{
    switch (event->type) {
        break;case SeatEventType::keyboard_key:
            if (!event->keyboard.key.pressed) return {};
            if (event->keyboard.key.code == KEY_Q) {
                return close_focused(wm,
                    seat_keyboard_get_seat(event->keyboard.keyboard),
                    seat_keyboard_get_focus(event->keyboard.keyboard));
            }
            if (event->keyboard.key.code == KEY_S) {
                auto mods = seat_keyboard_get_modifiers(event->keyboard.keyboard);
                if (mods.contains(wm->main_mod)) {
                    seat_keyboard_focus(event->keyboard.keyboard, nullptr);
                }
            }
        break;case SeatEventType::pointer_button:
            if (!event->pointer.button.pressed) return {};
            if (event->pointer.button.code == BTN_MIDDLE) {
                return close_focused(wm,
                    seat_pointer_get_seat(event->pointer.pointer),
                    seat_pointer_get_focus(event->pointer.pointer));
            }
        break;default:
            ;
    }

    return {};
}

void wm_init_hotkeys(WmServer* wm)
{
    wm->hotkeys.filter = seat_add_event_filter(wm_get_seat(wm), [wm](SeatEvent* event) {
        return filter_event(wm, event);
    });
}
