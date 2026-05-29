#include "internal.hpp"

#include <core/process.hpp>

auto wm_get_seat(WmServer* wm) -> Seat*
{
    debug_assert(wm->seats.size() == 1, "TODO: Support multiple seats");
    return wm->seats.front();
}

auto wm_get_seats(WmServer* wm) -> std::span<Seat* const>
{
    return wm->seats;
}

void wm_init_seat(WmServer* wm)
{
    wm->cursor_manager = seat_cursor_manager_create(wm->gpu,
        env_get("XCURSOR_THEME").transform(&std::string::c_str).value_or(nullptr),
        env_get<int>("XCURSOR_SIZE").value_or(24));

    auto keyboard = seat_keyboard_create({
        .layout = "gb",
        .rate   = 25,
        .delay  = 600,
    });

    auto pointer = seat_pointer_create({
        .cursor_manager = wm->cursor_manager.get(),
        .root = wm_get_scene(wm),
        .layer = wm_get_layer(wm, WmLayer::overlay),
    });

    auto seat = seat_create(wm_get_seat_manager(wm), "seat-0", keyboard.get(), pointer.get());
    wm->seats.emplace_back(seat.get());

    // Pointer

    seat_pointer_set_xcursor(pointer.get(), "default");
}
