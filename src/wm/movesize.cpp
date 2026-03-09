#include "wm.hpp"

static
void begin_interaction(wm_context* wm, scene_pointer* pointer, wm_movesize_mode initial_mode)
{
    scene_pointer_focus(pointer, wm->movesize.client.get());
    wm->movesize.pointer = pointer;

    auto pos = scene_pointer_get_position(pointer);
    auto* window = scene_find_window_at(wm->scene, pos);
    if (!window) return;
    auto frame = scene_window_get_frame(window);

    wm->movesize.mode = initial_mode;
    wm->movesize.window = window;
    wm->movesize.frame = frame;
    wm->movesize.grab = pos;

    auto dirs = (vec2i32(pos - frame.origin) * 3 / vec2i32(frame.extent)) - 1;

    wm->movesize.relative = {
        dirs.x || !dirs.y,
        dirs.y || !dirs.x,
    };

    if (initial_mode == wm_movesize_mode::move && dirs.y < 0) {
        wm->movesize.relative.x = 1;
    } else if (initial_mode == wm_movesize_mode::size) {
        if (!dirs.x && !dirs.y) {
            wm->movesize.mode = wm_movesize_mode::move;
        } else {
            wm->movesize.relative = dirs;
        }
    }
}

static
void end_interaction(wm_context* wm)
{
    core_assert(!wm->movesize.pointer);
    wm->movesize.mode = wm_movesize_mode::none;
}

// -----------------------------------------------------------------------------

static constexpr auto button_move = BTN_LEFT;
static constexpr auto button_size = BTN_RIGHT;

static
void handle_hotkey(wm_context* wm, scene_hotkey_event event)
{
    auto* pointer = scene_input_device_get_pointer(event.input_device);

    if (!event.pressed || wm->movesize.mode != wm_movesize_mode::none) {
        return;
    }

    switch (event.hotkey.code) {
        break;case button_move: begin_interaction(wm, pointer, wm_movesize_mode::move);
        break;case button_size: begin_interaction(wm, pointer, wm_movesize_mode::size);
    }
}

static
void handle_motion(wm_context* wm)
{
    if (!wm->movesize.window) {
        scene_pointer_focus(wm->movesize.pointer, nullptr);
        return;
    }

    auto pos = scene_pointer_get_position(wm->movesize.pointer);
    auto delta = (pos - wm->movesize.grab) * wm->movesize.relative;
    auto frame = wm->movesize.frame;

    if (wm->movesize.mode == wm_movesize_mode::move) {
        frame.origin += delta;

    } else if (wm->movesize.mode == wm_movesize_mode::size) {
        delta = glm::max(delta, 100.f - frame.extent);
        frame.origin += glm::min(wm->movesize.relative, {0,0}) * delta;
        frame.extent += delta;
    }

    scene_window_request_reposition(wm->movesize.window.get(), frame, wm->movesize.relative);
}

static
void handle_event(wm_context* wm, scene_event* event)
{
    switch (event->type) {
        break;case scene_event_type::hotkey:
            handle_hotkey(wm, event->hotkey);
        break;case scene_event_type::pointer_leave:
            wm->movesize.pointer = nullptr;
            end_interaction(wm);
        break;case scene_event_type::pointer_motion:
            handle_motion(wm);
        break;default:
            ;
    }
}

// -----------------------------------------------------------------------------

void wm_init_movesize(wm_context* wm)
{
    wm->movesize.client = scene_client_create(wm->scene);

    core_assert(scene_client_hotkey_register(wm->movesize.client.get(), {wm->main_mod, button_move}));
    core_assert(scene_client_hotkey_register(wm->movesize.client.get(), {wm->main_mod, button_size}));

    scene_client_set_event_handler(wm->movesize.client.get(), [wm](scene_event* event) {
        handle_event(wm, event);
    });
}
