#include "internal.hpp"

static
void begin_interaction(WindowManager* wm, ScenePointer* pointer, WmInteractionMode initial_mode)
{
    scene_pointer_focus(pointer, wm->focus.get());
    wm->movesize.pointer = pointer;

    auto pos = scene_pointer_get_position(pointer);
    auto* window = scene_find_window_at(wm->scene, pos);
    if (!window) return;
    auto frame = scene_window_get_frame(window);

    wm->mode = initial_mode;
    wm->movesize.window = window;
    wm->movesize.frame = frame;
    wm->movesize.grab = pos;

    auto dirs = (vec2i32(pos - frame.origin) * 3 / vec2i32(frame.extent)) - 1;

    wm->movesize.relative = {
        dirs.x || !dirs.y,
        dirs.y || !dirs.x,
    };

    if (initial_mode == WmInteractionMode::move && dirs.y < 0) {
        wm->movesize.relative.x = 1;
    } else if (initial_mode == WmInteractionMode::size) {
        if (!dirs.x && !dirs.y) {
            wm->mode = WmInteractionMode::move;
        } else {
            wm->movesize.relative = dirs;
        }
    }
}

static
void end_interaction(WindowManager* wm)
{
    debug_assert(!wm->movesize.pointer);
    wm->mode = WmInteractionMode::none;
}

// -----------------------------------------------------------------------------

static
void handle_hotkey(WindowManager* wm, SceneHotkeyEvent event)
{
    auto* pointer = scene_input_device_get_pointer(event.input_device);

    if (!event.pressed || wm->mode != WmInteractionMode::none) {
        return;
    }

    switch (event.hotkey.code) {
        break;case BTN_LEFT: begin_interaction(wm, pointer, WmInteractionMode::move);
        break;case BTN_RIGHT: begin_interaction(wm, pointer, WmInteractionMode::size);
    }
}

static
void handle_motion(WindowManager* wm)
{
    if (!wm->movesize.window) {
        scene_pointer_focus(wm->movesize.pointer, nullptr);
        return;
    }

    auto pos = scene_pointer_get_position(wm->movesize.pointer);
    auto delta = (pos - wm->movesize.grab) * wm->movesize.relative;
    auto frame = wm->movesize.frame;

    if (wm->mode == WmInteractionMode::move) {
        frame.origin += delta;

    } else if (wm->mode == WmInteractionMode::size) {
        delta = glm::max(delta, 100.f - frame.extent);
        frame.origin += glm::min(wm->movesize.relative, {0,0}) * delta;
        frame.extent += delta;
    }

    scene_window_request_reposition(wm->movesize.window.get(), frame, wm->movesize.relative);
}

void wm_movesize_handle_event(WindowManager* wm, SceneEvent* event)
{
    switch (event->type) {
        break;case SceneEventType::hotkey:
            handle_hotkey(wm, event->hotkey);
        break;case SceneEventType::pointer_leave:
            wm->movesize.pointer = nullptr;
            end_interaction(wm);
        break;case SceneEventType::pointer_motion:
            handle_motion(wm);
        break;default:
            ;
    }
}
