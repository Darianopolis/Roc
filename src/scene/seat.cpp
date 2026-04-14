#include "internal.hpp"

auto seat_create(SeatCursorManager* cursor_manager, SceneTree* pointer_root, SceneTree* pointer_layer) -> Ref<Seat>
{
    auto seat = ref_create<Seat>();

    seat->keyboard = seat_keyboard_create(seat.get());
    seat->pointer = seat_pointer_create(seat.get(), cursor_manager, pointer_root, pointer_layer);

    return seat;
}

auto seat_get_keyboard(Seat* seat) -> SeatKeyboard*
{
    return seat->keyboard.get();
}

auto seat_get_pointer(Seat* seat) -> SeatPointer*
{
    return seat->pointer.get();
}
