#include "internal.hpp"

#include "core/math.hpp"

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
void update_cursor(WindowManager* wm)
{
    auto size = vec2f32(6, 6);
    scene_texture_set_dst(wm->seat.cursor.get(), {wm->seat.position - size, size * 2.f, xywh});
}

void wm_seat_handle_io_event(WindowManager* wm, IoEvent* event)
{
    vec2f32 motion = {};
    vec2f32 scroll = {};

    WmLinearAccel accel {
        .offset     = 2.f,
        .rate       = 0.05f,
        .multiplier = 0.3f
    };

    for (auto& channel : event->input.channels) {
        switch (channel.type) {
            break;case EV_KEY:
                switch (channel.code) {
                    break;case BTN_MOUSE ... BTN_TASK:
                        if (channel.value) {
                            seat_pointer_press(wm->seat.pointer.get(), channel.code);
                        } else {
                            seat_pointer_release(wm->seat.pointer.get(), channel.code);
                        }

                    break;case KEY_ESC        ... KEY_MICMUTE:
                          case KEY_OK         ... KEY_LIGHTS_TOGGLE:
                          case KEY_ALS_TOGGLE ... KEY_PERFORMANCE:
                        if (channel.value) {
                            seat_keyboard_press(wm->seat.keyboard.get(), channel.code);
                        } else {
                            seat_keyboard_release(wm->seat.keyboard.get(), channel.code);
                        }

                    break;default:

                }
            break;case EV_REL:
                switch (channel.code) {
                    break;case REL_X: motion.x += channel.value;
                    break;case REL_Y: motion.y += channel.value;
                    break;case REL_HWHEEL: scroll.x += channel.value;
                    break;case REL_WHEEL:  scroll.y += channel.value;
                }
            break;case EV_ABS:
        }
    }

    if (motion.x || motion.y) {
        auto rel_accel = accel(motion);
        wm->seat.position += rel_accel;
        seat_pointer_move(wm->seat.pointer.get(),
            SeatPosition {
                .root = scene_get_root(wm->scene),
                .translation = wm->seat.position,
            },
            SeatMotion {
                .accel = rel_accel,
                .unaccel = motion,
            });
        update_cursor(wm);
    }

    if (scroll.x || scroll.y) {
        seat_pointer_scroll(wm->seat.pointer.get(), scroll);
    }
}

void wm_init_seat(WindowManager* wm)
{
    wm->seat.seat = seat_create();
    wm->seat.keyboard = seat_keyboard_create({
        .layout = "gb",
        .rate   = 25,
        .delay  = 600,
    });
    wm->seat.pointer = seat_pointer_create();

    wm->seat.cursor = scene_texture_create();
    scene_texture_set_tint(wm->seat.cursor.get(), {0, 255, 0, 255});
    scene_tree_place_above(wm_get_layer(wm, WmLayer::cursor), nullptr, wm->seat.cursor.get());
    update_cursor(wm);
}
