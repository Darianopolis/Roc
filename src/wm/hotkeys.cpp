#include "internal.hpp"

static
void close_focused(SceneEvent* event)
{
    if (!event->hotkey.pressed) return;
    auto focus = scene_input_device_get_focus(event->hotkey.input_device);
    if (focus && focus->window) {
        scene_window_request_close(focus->window.get());
    }
}

static
void unfocus(SceneEvent* event)
{
    if (!event->hotkey.pressed) return;
    auto keyboard = scene_input_device_get_keyboard(event->hotkey.input_device);
    if (!keyboard) return;
    scene_keyboard_focus(keyboard, nullptr);
}

void wm_init_hotkeys(WindowManager* wm)
{
    wm->hotkeys.client = scene_client_create(wm->scene);
    ankerl::unordered_dense::map<SceneHotkey, void(*)(SceneEvent*)> hotkeys;

    hotkeys[{ wm->main_mod, KEY_Q      }] = close_focused;
    hotkeys[{ wm->main_mod, BTN_MIDDLE }] = close_focused;
    hotkeys[{ wm->main_mod, KEY_S }]      = unfocus;

    for (auto&[hotkey, _] : hotkeys) {
        debug_assert(scene_client_hotkey_register(wm->hotkeys.client.get(), hotkey));
    }
    scene_client_set_event_handler(wm->hotkeys.client.get(), [hotkeys](SceneEvent* event) {
        if (event->type == SceneEventType::hotkey) {
            hotkeys.at(event->hotkey.hotkey)(event);
        }
    });
}
