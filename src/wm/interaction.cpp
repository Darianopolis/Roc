#include "internal.hpp"

static
void handle_event(WindowManager* wm, SceneEvent* event)
{
    switch (wm->mode) {
        break;case WmInteractionMode::none:
            if (event->type == SceneEventType::hotkey && event->hotkey.pressed) {
                auto hotkey = event->hotkey.hotkey;
                if (hotkey.code == BTN_LEFT && !hotkey.mod.contains(SceneModifier::shift)) {
                    wm_zone_handle_event(wm, event);
                } else {
                    wm_movesize_handle_event(wm, event);
                }
            }
        break;case WmInteractionMode::move:
              case WmInteractionMode::size:
            wm_movesize_handle_event(wm, event);
        break;case WmInteractionMode::zone:
            wm_zone_handle_event(wm, event);
    }
}

// -----------------------------------------------------------------------------

void wm_init_interaction(WindowManager* wm)
{
    wm->client = scene_client_create(wm->scene);
    wm->focus = scene_input_region_create(wm->client.get(), nullptr);

    debug_assert(scene_client_hotkey_register(wm->client.get(), {wm->main_mod, BTN_LEFT}));
    debug_assert(scene_client_hotkey_register(wm->client.get(), {wm->main_mod, BTN_RIGHT}));
    debug_assert(scene_client_hotkey_register(wm->client.get(), {wm->main_mod | SceneModifier::shift, BTN_LEFT}));
    debug_assert(scene_client_hotkey_register(wm->client.get(), {wm->main_mod | SceneModifier::shift, BTN_RIGHT}));

    scene_client_set_event_handler(wm->client.get(), [wm](SceneEvent* event) {
        handle_event(wm, event);
    });
}
