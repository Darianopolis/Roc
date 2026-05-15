#include "internal.hpp"

static
void end_interaction(WmServer* wm)
{
    wm->movesize.pointer = nullptr;
    wm->mode = WmInteractionMode::none;
}

static
void begin_interaction(WmServer* wm, SeatPointer* pointer, WmInteractionMode initial_mode)
{
    wm->movesize.pointer = pointer;

    auto pos = seat_pointer_get_position(pointer);
    auto* window = wm_find_window_at(wm, pos);
    if (!window) return;
    auto frame = wm_window_get_frame(window);

    if (initial_mode == WmInteractionMode::size && !wm_window_is_resizable(window)) {
        initial_mode = WmInteractionMode::move;
    }

    wm->mode = initial_mode;
    wm->movesize.window = window;
    wm->movesize.frame = frame;
    wm->movesize.grab = pos;

    auto dirs = (vec_cast<i32>(pos - frame.origin) * 3 / vec_cast<i32>(frame.extent)) - 1;

    wm->movesize.relative = {
        f32(dirs.x || !dirs.y),
        f32(dirs.y || !dirs.x),
    };

    if (initial_mode == WmInteractionMode::move && dirs.y < 0) {
        wm->movesize.relative.x = 1;
    } else if (initial_mode == WmInteractionMode::size) {
        if (!dirs.x && !dirs.y) {
            wm->mode = WmInteractionMode::move;
        } else {
            wm->movesize.relative = vec_cast<f32>(dirs);
        }
    }

    if (wm->mode == WmInteractionMode::move && !wm_window_is_movable(window)) {
        end_interaction(wm);
    }
}

// -----------------------------------------------------------------------------

static
void handle_motion(WmServer* wm)
{
    if (!wm->movesize.window) {
        return;
    }

    auto pos = seat_pointer_get_position(wm->movesize.pointer);
    auto delta = (pos - wm->movesize.grab) * wm->movesize.relative;
    auto frame = wm->movesize.frame;

    if (wm->mode == WmInteractionMode::move) {
        frame.origin += delta;

    } else if (wm->mode == WmInteractionMode::size) {
        delta = vec_max(delta, 100.f - frame.extent);
        frame.origin += vec_min(wm->movesize.relative, {0,0}) * delta;
        frame.extent += delta;
    }

    wm_window_request_reposition(wm->movesize.window.get(), frame, wm->movesize.relative);
}

static
auto filter_event_movesize(WmServer* wm, SeatEvent* event) -> SeatEventFilterResult
{
    switch (event->type) {
        break;case SeatEventType::pointer_motion:
            if (event->pointer.pointer == wm->movesize.pointer) handle_motion(wm);
        break;case SeatEventType::pointer_button:
            if (event->pointer.pointer == wm->movesize.pointer) {
                if (event->pointer.button.pressed) return SeatEventFilterResult::capture;
                if (seat_pointer_get_pressed(wm->movesize.pointer).empty()) {
                    end_interaction(wm);
                }
            }
        break;case SeatEventType::pointer_scroll:
            if (event->pointer.pointer == wm->movesize.pointer) return SeatEventFilterResult::capture;
        break;default:
            ;
    }

    return {};
}

static
auto filter_event_default(WmServer* wm, SeatEvent* event) -> SeatEventFilterResult
{
    if (event->type != SeatEventType::pointer_button) return {};

    auto button = event->pointer.button;
    if (!button.pressed) return {};

    auto mods = seat_get_modifiers(seat_pointer_get_seat(event->pointer.pointer));
    if (!mods.contains(wm->main_mod)) return {};

    if (button.code == BTN_LEFT && mods.contains(SeatModifier::shift)) {
        begin_interaction(wm, event->pointer.pointer, WmInteractionMode::move);
        return SeatEventFilterResult::capture;

    } else if (button.code == BTN_RIGHT) {
        begin_interaction(wm, event->pointer.pointer, WmInteractionMode::size);
        return SeatEventFilterResult::capture;
    }

    return {};
}
static

auto filter_event(WmServer* wm, SeatEvent* event) -> SeatEventFilterResult
{
    switch (wm->mode) {
        break;case WmInteractionMode::none:
            return filter_event_default(wm, event);
        break;case WmInteractionMode::move:
              case WmInteractionMode::size:
            return filter_event_movesize(wm, event);
        break;default:
            ;
    }

    return SeatEventFilterResult::passthrough;
}

void wm_init_movesize(WmServer* wm)
{
    wm->movesize.filter = seat_add_event_filter(wm_get_seat(wm), [wm](SeatEvent* event) {
        return filter_event(wm, event);
    });
}
