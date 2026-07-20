#include "internal.hpp"

#include <core/process.hpp>
#include <core/log.hpp>

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
                    break;case KEY_K:
                        server->debug.show_damage = !server->debug.show_damage;
                        wm_toast(server, std::format("Show scene damage: {}", server->debug.show_damage ? "Enabled" : "Disabled"));
                        for (auto* output : server->io.outputs) {
                            output->needs_redraw = true;
                            output->primary_damage = Region<f32>{output->viewport};
                            output->interface.request_frame(output->userdata);
                        }
                        return SeatEventFilterResult::capture;
                    break;case KEY_A: {
                        server->config.pointer.accel.state = WmPointerAccelState((num_cast<u32>(server->config.pointer.accel.state) + 1)
                                                                                % num_cast<u32>(enum_values<WmPointerAccelState>().size()));
                        auto name = enum_name(server->config.pointer.accel.state);
                        wm_toast(server, std::format("Pointer acceleration: {}{}", char(std::toupper(name[0])), name.substr(1)));
                        return SeatEventFilterResult::capture;
                    }
                    break;case KEY_L:
                        server->debug.disable_cursor_plane = !server->debug.disable_cursor_plane;
                        wm_toast(server, std::format("Cursor plane: {}", server->debug.disable_cursor_plane ? "Disabled" : "Enabled"));
                        for (auto* output : server->io.outputs) {
                            output->needs_redraw = true;
                            output->interface.request_frame(output->userdata);
                        }
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
