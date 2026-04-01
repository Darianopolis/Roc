#include "internal.hpp"

void scene_seat_init(Scene* scene)
{
    auto seat = ref_create<SceneSeat>();
    seat->scene = scene;
    scene->seats.emplace_back(seat.get());

    seat->keyboard = scene_keyboard_create(seat.get());
    seat->pointer = scene_pointer_create(seat.get());
}

auto scene_get_seats(Scene* scene) -> std::span<SceneSeat* const>
{
    return scene->seats;
}

auto scene_get_exclusive_seat(Scene* scene) -> SceneSeat*
{
    debug_assert(scene->seats.size() == 1, "TODO: Multi-seat support");
    return scene->seats.front();
}

auto scene_seat_get_keyboard(SceneSeat* seat) -> SceneKeyboard*
{
    return seat->keyboard.get();
}

auto scene_seat_get_pointer(SceneSeat* seat) -> ScenePointer*
{
    return seat->pointer.get();
}
