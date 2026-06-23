#include "internal.hpp"

#include <core/process.hpp>

auto wm_get_seat(WmServer* server) -> Seat*
{
    debug_assert(server->seats.size() == 1, "TODO: Support multiple seats");
    return server->seats.front();
}

auto wm_get_seats(WmServer* server) -> std::span<Seat* const>
{
    return server->seats;
}

void wm_init_seat(WmServer* server)
{
    server->cursor_manager = seat_cursor_manager_create(server->gpu,
        env_get("XCURSOR_THEME").transform(&std::string::c_str).value_or(nullptr),
        env_get<int>("XCURSOR_SIZE").value_or(24));

    auto keyboard = seat_keyboard_create({
        .layout = "gb",
        .rate   = 25,
        .delay  = 600,
    });

    auto pointer = seat_pointer_create({
        .cursor_manager = server->cursor_manager.get(),
        .root = wm_get_scene(server),
        .layer = wm_get_layer(server, WmLayer::cursor),
    });

    auto seat = seat_create(wm_get_seat_manager(server), "seat-0", keyboard.get(), pointer.get());
    server->seats.emplace_back(seat.get());

    // Pointer

    seat_pointer_set_xcursor(pointer.get(), "default");
}
