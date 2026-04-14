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

auto wm_get_seat(WindowManager* wm) -> Seat*
{
    debug_assert(wm->seats.size() == 1, "TODO: Support multiple seats");
    return wm->seats.front();
}

auto wm_get_seats(WindowManager* wm) -> std::span<Seat* const>
{
    return wm->seats;
}

void wm_init_seat(WindowManager* wm)
{
    wm->cursor_manager = scene_cursor_manager_create(wm->gpu, "breeze_cursors", 24);

    auto seat = seat_create(wm->cursor_manager.get(), scene_get_root(wm->scene.get()), wm_get_layer(wm, WmLayer::overlay));
    wm->seats.emplace_back(seat.get());

    // Pointer

    auto* pointer = seat_get_pointer(seat.get());
    seat_pointer_set_xcursor(pointer, "default");
    seat_pointer_set_accel(  pointer, WmLinearAccel {
        .offset     = 2.f,
        .rate       = 0.05f,
        .multiplier = 0.3f
    });
}
