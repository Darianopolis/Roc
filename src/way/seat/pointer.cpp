#include "internal.hpp"

// -----------------------------------------------------------------------------

void way_seat_get_pointer(wl_client* wl_client, wl_resource* resource, u32 id)
{
    auto* seat_client = way_get_userdata<WaySeatClient>(resource);

    seat_client->pointers.emplace_back(way_resource_create_refcounted(wl_pointer, wl_client, resource, id, seat_client));
}

static
void set_cursor(wl_client* wl_client, wl_resource* resource, u32 serial, wl_resource* wl_surface, i32 hot_x, i32 hot_y)
{
    auto* seat_client = way_get_userdata<WaySeatClient>(resource);
    auto* seat = seat_client->seat;
    auto* surface = wl_surface ? way_get_userdata<WaySurface>(wl_surface) : nullptr;

    if (surface) {
        scene_tree_set_translation(surface->scene.tree.get(), {-hot_x, -hot_y});

        if (surface->role != WaySurfaceRole::cursor) {
            surface->role = WaySurfaceRole::cursor;
            surface->cursor_role = ref_create<WayCursorSurface>();
            way_surface_addon_register(surface, surface->cursor_role.get());
            scene_node_unparent(surface->scene.input_region.get());
        }
    }

    if (seat->pointer.scene) {
        seat_pointer_set_cursor(seat->pointer.scene, surface ? surface->scene.tree.get() : nullptr);
    }
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
auto to_surface_pos(WaySurface* surface, vec2f32 global_pos)
{
    return global_pos - scene_tree_get_position(surface->scene.tree.get());
}

static
void pointer_leave_surface(WaySeatClient* seat_client, WaySurface* surface, WaySerial serial)
{
    if (!surface->resource) return;
    for (auto* resource : seat_client->pointers) {
        way_send(wl_pointer_send_leave, resource, serial.value, surface->resource);
    }
}

void way_seat_on_pointer_leave(WaySeatClient* seat_client, SeatEvent* event)
{
    auto* seat = seat_client->seat;

    if (auto* surface = seat->focus.pointer.get()) {
        pointer_leave_surface(seat_client, surface, way_next_serial(seat->server));
    }

    seat->focus.pointer = nullptr;
    seat->pointer.scene = nullptr;
}

void way_seat_on_pointer_enter(WaySeatClient* seat_client, SeatEvent* event)
{
    auto* seat = seat_client->seat;
    auto* server = seat->server;

    seat->pointer.scene = event->pointer.pointer;

    auto serial = way_next_serial(server);

    auto* old_surface = seat->focus.pointer.get();
    auto* new_surface = find_surface(seat_client->client, event->pointer.focus);

    if (old_surface && new_surface && old_surface != new_surface) {
        pointer_leave_surface(seat_client, old_surface, serial);
    }

    seat->focus.pointer = new_surface;

    if (!new_surface->resource) return;
    auto pos = to_fixed(to_surface_pos(new_surface, seat_pointer_get_position(seat->pointer.scene)));
    for (auto* resource : seat_client->pointers) {
        way_send(wl_pointer_send_enter, resource, serial.value, new_surface->resource, pos.x, pos.y);
    }
}

// -----------------------------------------------------------------------------

static
void pointer_frame(WayServer* server, wl_resource* resource)
{
    if (wl_resource_get_version(resource) >= WL_POINTER_FRAME_SINCE_VERSION) {
        way_send(wl_pointer_send_frame, resource);
    }
}

void way_seat_on_motion(WaySeatClient* seat_client, SeatEvent* event)
{
    auto* seat = seat_client->seat;
    auto* server = seat->server;

    auto* surface = seat->focus.pointer.get();
    if (!surface) return;

    auto elapsed = way_get_elapsed(server);
    u64 time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

    auto pos = to_fixed(to_surface_pos(surface, seat_pointer_get_position(seat->pointer.scene)));

    {
        u64 time_us = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
        u32 time_us_hi = time_us >> 32;
        u32 time_us_lo = time_us & ~0u;

        auto accel   = to_fixed(event->pointer.motion.rel_accel);
        auto unaccel = to_fixed(event->pointer.motion.rel_unaccel);
        for (auto* resource : seat_client->relative_pointers) {
            way_send(zwp_relative_pointer_v1_send_relative_motion,
                resource, time_us_hi, time_us_lo, accel.x, accel.y, unaccel.x, unaccel.y);
        }
    }

    for (auto* resource : seat_client->pointers) {
        way_send(wl_pointer_send_motion, resource, time_ms, pos.x, pos.y);
        pointer_frame(server, resource);
    }
}

void way_seat_on_button(WaySeatClient* seat_client, SeatEvent* event)
{
    auto* seat = seat_client->seat;
    auto* server = seat->server;

    auto serial = way_next_serial(server);
    auto elapsed = way_get_elapsed(server);
    u64 time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    auto button = event->pointer.button;
    auto state = button.pressed ? WL_POINTER_BUTTON_STATE_PRESSED : WL_POINTER_BUTTON_STATE_RELEASED;

    for (auto* resource : seat_client->pointers) {
        way_send(wl_pointer_send_button, resource, serial.value, time_ms, button.code, state);
        pointer_frame(server, resource);
    }
}

void way_seat_on_scroll(WaySeatClient* seat_client, SeatEvent* event)
{
    auto* seat = seat_client->seat;
    auto* server = seat->server;

    auto elapsed = way_get_elapsed(server);
    u64 time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    auto delta = event->pointer.scroll.delta;

    static constexpr f32 axis_pixel_rate = 15.f;

    for (auto* resource : seat_client->pointers) {
        auto version = wl_resource_get_version(resource);

        if (version >= WL_POINTER_AXIS_SOURCE_SINCE_VERSION) {
            way_send(wl_pointer_send_axis_source, resource, WL_POINTER_AXIS_SOURCE_WHEEL);
        }

        if (delta.x) way_send(wl_pointer_send_axis, resource, time_ms, WL_POINTER_AXIS_HORIZONTAL_SCROLL, wl_fixed_from_double(delta.x * axis_pixel_rate));
        if (delta.y) way_send(wl_pointer_send_axis, resource, time_ms, WL_POINTER_AXIS_VERTICAL_SCROLL,   wl_fixed_from_double(delta.y * axis_pixel_rate));

        if (version >= WL_POINTER_AXIS_VALUE120_SINCE_VERSION) {
            if (delta.x) way_send(wl_pointer_send_axis_value120, resource, WL_POINTER_AXIS_HORIZONTAL_SCROLL, i32(delta.x * 120));
            if (delta.y) way_send(wl_pointer_send_axis_value120, resource, WL_POINTER_AXIS_VERTICAL_SCROLL,   i32(delta.y * 120));

        } else if (version >= WL_POINTER_AXIS_DISCRETE_SINCE_VERSION) {
            // TODO: Accumulate fractional values
            if (delta.x) way_send(wl_pointer_send_axis_discrete, resource, WL_POINTER_AXIS_HORIZONTAL_SCROLL, i32(delta.x));
            if (delta.y) way_send(wl_pointer_send_axis_discrete, resource, WL_POINTER_AXIS_VERTICAL_SCROLL,   i32(delta.y));
        }

        pointer_frame(server, resource);
    }
}

void WayCursorSurface::apply(WayCommitId)
{
    if (surface->current.surface.offset != vec2i32{}) {
        scene_tree_set_translation(surface->scene.tree.get(),
            surface->scene.tree->translation + vec2f32(surface->current.surface.offset));

        surface->current.surface.offset = {};
    }
}
