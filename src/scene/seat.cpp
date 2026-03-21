#include "internal.hpp"

void scene_seat_init(scene_context* ctx)
{
    auto seat = core_create<scene_seat>();
    seat->ctx = ctx;
    ctx->seats.emplace_back(seat.get());

    seat->keyboard = scene_keyboard_create(seat.get());
    seat->pointer = scene_pointer_create(seat.get());
}

auto scene_get_seats(scene_context* ctx) -> std::span<scene_seat* const>
{
    return ctx->seats;
}

auto scene_get_exclusive_seat(scene_context* ctx) -> scene_seat*
{
    core_assert(ctx->seats.size() == 1, "TODO: Multi-seat support");
    return ctx->seats.front();
}

auto scene_seat_get_keyboard(scene_seat* seat) -> scene_keyboard*
{
    return seat->keyboard.get();
}

auto scene_seat_get_pointer(scene_seat* seat) -> scene_pointer*
{
    return seat->pointer.get();
}
