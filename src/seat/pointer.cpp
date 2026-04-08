#include "seat.hpp"

#include "core/math.hpp"

auto seat_pointer_create() -> Ref<SeatPointer>
{
    return ref_create<SeatPointer>();
}

void seat_pointer_press(SeatPointer* pointer, SeatInputCode code)
{
    if (pointer->pressed.inc(code)) {
        log_warn("pointer button pressed: {}", libevdev_event_code_get_name(EV_KEY, code));
    }
}

void seat_pointer_release(SeatPointer* pointer, SeatInputCode code)
{
    if (pointer->pressed.dec(code)) {
        log_warn("pointer button released: {}", libevdev_event_code_get_name(EV_KEY, code));
    }
}

void seat_pointer_scroll(SeatPointer* pointer, vec2f32 delta)
{
    log_warn("pointer scrolled {}", delta);
}

void seat_pointer_move(SeatPointer* pointer, SeatPosition position, SeatMotion motion)
{
    pointer->position = position;

    log_warn("pointer moved");
    log_warn("      pos = {}", position.translation);
    log_warn("    accel = {}", motion.accel);
    log_warn("  unaccel = {}", motion.unaccel);
}
