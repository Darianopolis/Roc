#include "internal.hpp"

// -----------------------------------------------------------------------------

void way_seat_get_pointer(wl_client* wl_client, wl_resource* resource, u32 id)
{
    auto* client_seat = way_get_userdata<WayClientSeat>(resource);

    client_seat->pointers.emplace_back(way_resource_create_refcounted(wl_pointer, wl_client, resource, id, client_seat));
}

static
void set_cursor(wl_client* wl_client, wl_resource* resource, u32 serial, wl_resource* wl_surface, i32 hot_x, i32 hot_y)
{
    auto* client_seat = way_get_userdata<WayClientSeat>(resource);
    auto* cursor_surface = wl_surface ? way_get_userdata<WaySurface>(wl_surface) : nullptr;

    way_set_cursor(client_seat, cursor_surface, {f32(-hot_x), f32(-hot_y)});
}

WAY_INTERFACE(wl_pointer) = {
    .set_cursor = set_cursor,
    .release = way_simple_destroy,
};

// -----------------------------------------------------------------------------

static
auto to_fixed(vec2f32 v) -> Vec<2, wl_fixed_t>
{
    return {wl_fixed_from_double(v.x), wl_fixed_from_double(v.y)};
}

static
void pointer_frame(WayServer* server, wl_resource* resource)
{
    if (wl_resource_get_version(resource) >= WL_POINTER_FRAME_SINCE_VERSION) {
        way_send<wl_pointer_send_frame>(resource);
    }
}

void way_seat_on_pointer_leave(WayClientSeat* client_seat, SeatEvent* event)
{
    auto* seat = client_seat->seat;

    auto surface = seat->focus.pointer;
    seat->focus.pointer = nullptr;
    if (!surface || !surface->resource) return;

    auto serial = way_next_serial(seat->server);
    for (auto* pointer : client_seat->pointers) {
        way_send<wl_pointer_send_leave>(pointer, serial.value, surface->resource);
        pointer_frame(client_seat->seat->server, pointer);
    }

}

void way_seat_on_pointer_enter(WayClientSeat* client_seat, SeatEvent* event)
{
    auto* seat = client_seat->seat;
    auto* server = seat->server;

    auto serial = way_next_serial(server);

    auto* surface = way_find_surface_for_focus(client_seat->client, event->pointer.focus);
    if (!surface || !surface->resource) return;

    seat->focus.pointer = surface;

    auto pos = to_fixed(event->pointer.position);

    for (auto* resource : client_seat->pointers) {
        way_send<wl_pointer_send_enter>(resource, serial.value, surface->resource, pos.x, pos.y);
        pointer_frame(server, resource);
    }
}

// -----------------------------------------------------------------------------

void way_seat_on_motion(WayClientSeat* client_seat, SeatEvent* event)
{
    auto* seat = client_seat->seat;
    auto* server = seat->server;

    auto* surface = seat->focus.pointer.get();
    if (!surface) return;

    auto elapsed = way_get_elapsed(server);
    u64 time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

    auto pos = to_fixed(event->pointer.position);

    {
        u64 time_us = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
        u32 time_us_hi = time_us >> 32;
        u32 time_us_lo = time_us & ~0u;

        auto accel   = to_fixed(event->pointer.motion.rel_accel);
        auto unaccel = to_fixed(event->pointer.motion.rel_unaccel);
        for (auto* resource : client_seat->relative_pointers) {
            way_send<zwp_relative_pointer_v1_send_relative_motion>(
                resource, time_us_hi, time_us_lo, accel.x, accel.y, unaccel.x, unaccel.y);
        }
    }

    for (auto* resource : client_seat->pointers) {
        way_send<wl_pointer_send_motion>(resource, time_ms, pos.x, pos.y);
        pointer_frame(server, resource);
    }
}

static
auto find_root_toplevel(WaySurface* surface) -> WaySurface*
{
    if (surface->role == WaySurfaceRole::xdg_toplevel) return surface;
    debug_assert(surface->parent);
    return find_root_toplevel(surface->parent.get());
}

void way_seat_on_button(WayClientSeat* client_seat, SeatEvent* event)
{
    auto* seat = client_seat->seat;
    auto* server = seat->server;

    auto serial = way_next_serial(server);
    auto elapsed = way_get_elapsed(server);
    u64 time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    auto button = event->pointer.button;
    auto state = button.pressed ? WL_POINTER_BUTTON_STATE_PRESSED : WL_POINTER_BUTTON_STATE_RELEASED;

    for (auto* resource : client_seat->pointers) {
        way_send<wl_pointer_send_button>(resource, serial.value, time_ms, button.code, state);
        pointer_frame(server, resource);
    }

    // Request focus on press

    if (event->pointer.button.pressed) {
        if (auto* toplevel = find_root_toplevel(seat->focus.pointer.get())) {
            auto* seat_keyboard = seat_get_keyboard(seat->seat);
            seat_keyboard_focus(seat_keyboard, toplevel->scene.focus.get());
        }
    }
}

void way_seat_on_scroll(WayClientSeat* client_seat, SeatEvent* event)
{
    auto* seat = client_seat->seat;
    auto* server = seat->server;

    auto elapsed = way_get_elapsed(server);
    u64 time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    auto delta = event->pointer.scroll.delta;

    static constexpr f32 axis_pixel_rate = 15.f;

    for (auto* resource : client_seat->pointers) {
        auto version = wl_resource_get_version(resource);

        if (version >= WL_POINTER_AXIS_SOURCE_SINCE_VERSION) {
            way_send<wl_pointer_send_axis_source>(resource, WL_POINTER_AXIS_SOURCE_WHEEL);
        }

        if (delta.x) way_send<wl_pointer_send_axis>(resource, time_ms, WL_POINTER_AXIS_HORIZONTAL_SCROLL, wl_fixed_from_double(delta.x * axis_pixel_rate));
        if (delta.y) way_send<wl_pointer_send_axis>(resource, time_ms, WL_POINTER_AXIS_VERTICAL_SCROLL,   wl_fixed_from_double(delta.y * axis_pixel_rate));

        if (version >= WL_POINTER_AXIS_VALUE120_SINCE_VERSION) {
            if (delta.x) way_send<wl_pointer_send_axis_value120>(resource, WL_POINTER_AXIS_HORIZONTAL_SCROLL, i32(delta.x * 120));
            if (delta.y) way_send<wl_pointer_send_axis_value120>(resource, WL_POINTER_AXIS_VERTICAL_SCROLL,   i32(delta.y * 120));

        } else if (version >= WL_POINTER_AXIS_DISCRETE_SINCE_VERSION) {
            // TODO: Accumulate fractional values
            if (delta.x) way_send<wl_pointer_send_axis_discrete>(resource, WL_POINTER_AXIS_HORIZONTAL_SCROLL, i32(delta.x));
            if (delta.y) way_send<wl_pointer_send_axis_discrete>(resource, WL_POINTER_AXIS_VERTICAL_SCROLL,   i32(delta.y));
        }

        pointer_frame(server, resource);
    }
}

void WayCursorSurface::apply(WayCommitId)
{
    if (surface->current.surface.offset != vec2i32{}) {
        scene_tree_set_translation(surface->scene.tree.get(),
            surface->scene.tree->translation + vec_cast<f32>(surface->current.surface.offset));

        surface->current.surface.offset = {};
    }
}
