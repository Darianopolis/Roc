#include "internal.hpp"

static
void end_interaction(WmServer* server)
{
    server->movesize.pointer = nullptr;
    wm_interaction_set_mode(server, WmInteractionMode::none);
}

static
void begin_interaction(WmServer* server, SeatPointer* pointer, WmInteractionMode mode)
{
    server->movesize.pointer = pointer;

    auto pos = seat_pointer_get_position(pointer);
    auto* window = wm_find_window_at(server, pos);
    if (!window) return;
    auto frame = wm_window_get_frame(window);

    server->movesize.window = window;
    server->movesize.frame = frame;
    server->movesize.grab = pos;

    auto dirs = (vec_cast<i32>(pos - frame.origin) * 3 / vec_cast<i32>(frame.extent)) - 1;

    server->movesize.relative = {
        num_cast<f32>(dirs.x || !dirs.y),
        num_cast<f32>(dirs.y || !dirs.x),
    };

    if (mode == WmInteractionMode::move && dirs.y < 0) {
        server->movesize.relative.x = 1;
    } else if (mode == WmInteractionMode::size) {
        if (!dirs.x && !dirs.y) {
            mode = WmInteractionMode::move;
        } else {
            server->movesize.relative = vec_cast<f32>(dirs);
        }
    }

    wm_interaction_set_mode(server, mode);

    switch (server->mode) {
        break;case WmInteractionMode::move: if (!wm_window_is_movable(window))   end_interaction(server);
        break;case WmInteractionMode::size: if (!wm_window_is_resizable(window)) end_interaction(server);
        break;default:
            ;
    }
}

// -----------------------------------------------------------------------------

static
void handle_motion(WmServer* server)
{
    if (!server->movesize.window) {
        return;
    }

    auto pos = seat_pointer_get_position(server->movesize.pointer);
    auto delta = (pos - server->movesize.grab) * server->movesize.relative;
    auto frame = server->movesize.frame;

    if (server->mode == WmInteractionMode::move) {
        frame.origin += delta;

    } else if (server->mode == WmInteractionMode::size) {
        delta = vec_max(delta, 100.f - frame.extent);
        frame.origin += vec_min(server->movesize.relative, {0,0}) * delta;
        frame.extent += delta;
    }

    wm_window_request_reposition(server->movesize.window.get(), frame, server->movesize.relative);
}

static
auto filter_event_movesize(WmServer* server, SeatEvent* event) -> SeatEventFilterResult
{
    switch (event->type) {
        break;case SeatEventType::pointer_motion:
            if (event->pointer.pointer == server->movesize.pointer) handle_motion(server);
        break;case SeatEventType::pointer_button:
            if (event->pointer.pointer == server->movesize.pointer) {
                if (event->pointer.button.pressed) return SeatEventFilterResult::capture;
                if (seat_pointer_get_pressed(server->movesize.pointer).empty()) {
                    end_interaction(server);
                }
            }
        break;case SeatEventType::pointer_scroll:
            if (event->pointer.pointer == server->movesize.pointer) return SeatEventFilterResult::capture;
        break;default:
            ;
    }

    return {};
}

static
auto filter_event_default(WmServer* server, SeatEvent* event) -> SeatEventFilterResult
{
    if (event->type != SeatEventType::pointer_button) return {};

    auto button = event->pointer.button;
    if (!button.pressed) return {};

    auto mods = seat_get_modifiers(seat_pointer_get_seat(event->pointer.pointer));
    if (!mods.contains(server->main_mod)) return {};

    if (button.code == BTN_LEFT && mods.contains(SeatModifier::shift)) {
        begin_interaction(server, event->pointer.pointer, WmInteractionMode::move);
        return SeatEventFilterResult::capture;

    } else if (button.code == BTN_RIGHT) {
        begin_interaction(server, event->pointer.pointer, WmInteractionMode::size);
        return SeatEventFilterResult::capture;
    }

    return {};
}
static

auto filter_event(WmServer* server, SeatEvent* event) -> SeatEventFilterResult
{
    switch (server->mode) {
        break;case WmInteractionMode::none:
            return filter_event_default(server, event);
        break;case WmInteractionMode::move:
              case WmInteractionMode::size:
            return filter_event_movesize(server, event);
        break;default:
            ;
    }

    return SeatEventFilterResult::passthrough;
}

void wm_init_movesize(WmServer* server)
{
    server->movesize.filter = seat_add_event_filter(wm_get_seat(server), [server](SeatEvent* event) {
        return filter_event(server, event);
    });
}
