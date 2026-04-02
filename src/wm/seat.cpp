#include "internal.hpp"

struct WmLinearAccel
{
    f32 offset;
    f32 rate;
    f32 multiplier;

    auto operator()(vec2f32 delta) -> vec2f32
    {
        // Apply a linear mouse acceleration curve
        //
        // Offset     - speed before acceleration is applied.
        // Accel      - rate that sensitivity increases with motion.
        // Multiplier - total multplier for sensitivity.
        //
        //      /
        //     / <- Accel
        // ___/
        //  ^-- Offset

        f32 speed = glm::length(delta);
        vec2f32 sens = vec2f32(multiplier * (1 + (std::max(speed, offset) - offset) * rate));

        return delta * sens;
    }
};

static
void handle_seat_event(WindowManager* wm, SceneEvent* event)
{
    switch (event->type) {
        break;case SceneEventType::seat_add: {
            auto* pointer = scene_seat_get_pointer(event->seat);
            scene_pointer_set_xcursor(pointer, "default");
            scene_pointer_set_accel(  pointer, WmLinearAccel {
                .offset     = 2.f,
                .rate       = 0.05f,
                .multiplier = 0.3f
            });
        }
        break;default:
            ;
    }
}

void wm_init_seat(WindowManager* wm)
{
    // Pointer

    wm->seat.client = scene_client_create(wm->scene);
    scene_client_set_event_handler(wm->seat.client.get(), [wm](SceneEvent* event) {
        handle_seat_event(wm, event);
    });
}
