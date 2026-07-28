#include "internal.hpp"

#include <core/process.hpp>
#include <core/log.hpp>

WmHotkey::~WmHotkey()
{
    if (!server) return;
    std::erase(server->hotkeys.map.at(code), this);
}

auto wm_bind_hotkey(WmServer* server, Flags<SeatModifier> modifiers, SeatInputCode code, std::function<WmHotkeyCallback> callback) -> Ref<WmHotkey>
{
    auto& hotkeys = server->hotkeys.map[code];
    debug_assert(!std::ranges::contains(hotkeys, modifiers, [&](WmHotkey* hotkey) { return hotkey->modifiers; }),
        "A hotkey for {} {} already exists", libevdev_event_code_get_name(EV_KEY, code), modifiers);

    auto hotkey = ref_create<WmHotkey>();
    hotkey->server = server;
    hotkey->modifiers = modifiers;
    hotkey->code = code;
    hotkey->callback = std::move(callback);

    hotkeys.emplace_back(hotkey.get());

    return hotkey;
}

static
auto filter_event(WmServer* server, SeatEvent* event) -> SeatEventFilterResult
{
    Flags<SeatModifier> modifiers = seat_get_modifiers(wm_get_seat(server), SeatModifierFlag::ignore_locked);
    SeatInputCode code;
    Seat* seat;
    SeatFocus* focus;
    switch (event->type) {
        break;case SeatEventType::keyboard_key:
            if (!event->keyboard.key.pressed) return SeatEventFilterResult::passthrough;
            code = event->keyboard.key.code;
            seat = seat_keyboard_get_seat(event->keyboard.keyboard);
            focus = seat_keyboard_get_focus(event->keyboard.keyboard);
        break;case SeatEventType::pointer_button:
            if (!event->pointer.button.pressed) return SeatEventFilterResult::passthrough;
            code = event->pointer.button.code;
            seat = seat_pointer_get_seat(event->pointer.pointer);
            focus = seat_pointer_get_focus(event->pointer.pointer);
        break;default:
            return SeatEventFilterResult::passthrough;
    }

    auto hotkeys = server->hotkeys.map.find(code);
    if (hotkeys == server->hotkeys.map.end()) return SeatEventFilterResult::passthrough;

    for (auto* hotkey : hotkeys->second) {
        if (modifiers == hotkey->modifiers) {
            hotkey->callback(seat, focus);
            return SeatEventFilterResult::capture;
        }
    }

    return SeatEventFilterResult::passthrough;
}

static
void close_focused(WmServer* server, Seat* seat, SeatFocus* focus)
{
    WmWindow* window;
    if (focus && (window = wm_find_window_for(server, focus))) {
        wm_window_request_close(window);
    }
}

void wm_init_hotkeys(WmServer* server)
{
    server->hotkeys.filter = seat_add_event_filter(wm_get_seat(server), [server](SeatEvent* event) {
        return filter_event(server, event);
    });

    auto hotkey = [&](SeatInputCode code, std::function<WmHotkeyCallback> callback) {
        server->hotkeys.builtins.emplace_back(wm_bind_hotkey(server, server->main_mod, code, callback));
    };

    // Close focused
    hotkey(KEY_Q,      [server](Seat* seat, SeatFocus* focus) { close_focused(server, seat, focus); });
    hotkey(BTN_MIDDLE, [server](Seat* seat, SeatFocus* focus) { close_focused(server, seat, focus); });

    // Clear focus
    hotkey(KEY_S, [server](auto...) { wm_focus(server, nullptr); });

    // Toggle cursor acceleration
    hotkey(KEY_A, [server](auto...) {
        server->config.pointer.accel.state = WmPointerAccelState((num_cast<u32>(server->config.pointer.accel.state) + 1)
                                                                % num_cast<u32>(enum_values<WmPointerAccelState>().size()));
        auto name = enum_name(server->config.pointer.accel.state);
        wm_toast(server, std::format("Pointer acceleration: {}{}", char(std::toupper(name[0])), name.substr(1)));
    });

    // Fullscreen
    hotkey(KEY_F, [server](Seat* seat, SeatFocus* focus) {
        auto window = wm_find_window_for(server, focus);
        if (!window && !server->windows.empty()) {
            window = server->windows.back();
        }
        if (window) {
            auto pointer = seat_get_pointer(seat);
            auto output = wm_find_output_at(server, seat_pointer_get_position(pointer)).output;
            if (wm_window_get_fullscreen(window) == output) {
                wm_window_set_fullscreen(window, nullptr);
            } else {
                wm_window_set_fullscreen(window, output);
            }
        }
    });

    // DEBUG : Toggle scene damage visualization
    hotkey(KEY_K, [server](auto...) {
        server->debug.show_damage = !server->debug.show_damage;
        wm_toast(server, std::format("Show scene damage: {}", server->debug.show_damage ? "Enabled" : "Disabled"));
        for (auto* output : server->io.outputs) {
            wm_output_damage(output);
        }
    });

    // DEBUG : Toggle cursor plane
    hotkey(KEY_L, [server](auto...) {
        server->debug.disable_cursor_plane = !server->debug.disable_cursor_plane;
        wm_toast(server, std::format("Cursor plane: {}", server->debug.disable_cursor_plane ? "Disabled" : "Enabled"));
        for (auto* output : server->io.outputs) {
            wm_output_damage(output);
        }
    });
}
