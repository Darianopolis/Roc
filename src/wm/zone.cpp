#include "internal.hpp"

#include <core/math.hpp>
#include <core/color.hpp>

static
auto get_zone_rect(WmServer* server, rect2i32 workarea, vec2u32 zone) -> rect2i32
{
    auto& c = server->config;

    rect2i32 out;

    auto get_axis = [&](usz axis) {
        i32 usable_length = workarea.extent[axis] - (c.zone.spacing * (num_cast<i32>(c.zone.count[axis]) - 1));
        f64 ideal_zone_size = num_cast<f64>(usable_length) / c.zone.count[axis];
        out.origin[axis] = num_cast<i32>(std::round(ideal_zone_size *  zone[axis]));
        out.extent[axis] = num_cast<i32>(std::round(ideal_zone_size * (zone[axis] + 1)) - out.origin[axis]);
        out.origin[axis] += workarea.origin[axis] + c.zone.spacing * num_cast<i32>(zone[axis]);
    };
    get_axis(0);
    get_axis(1);

    return out;
}

static
void update_rectangle(WmServer* server)
{
    bool show = server->zone.pointer;
    bool selecting = server->zone.selecting;
    rect2f64 rect = server->zone.final_zone;

    if (!show) {
        scene_node_unparent(server->zone.texture.get());
        return;
    }

    auto color = selecting ? server->config.zone.color_selected : server->config.zone.color_initial;

    scene_tree_place_below(wm_get_layer(server, WmLayer::overlay), nullptr, server->zone.texture.get());
    scene_texture_set_dst(server->zone.texture.get(), rect_cast<f32>(rect));
    scene_texture_set_tint(server->zone.texture.get(), color);
}

static
void zone_update_regions(WmServer* server)
{
    auto& c = server->config;

    auto pointer = server->zone.pointer;
    vec2f32 point = seat_pointer_get_position(pointer);

    auto[output, position] = wm_find_output_at(server, point);

    auto workarea = rect_cast<i32>(wm_output_get_workarea(output));

    aabb2f64 pointer_zone = {};
    bool any_zones = false;

    for (u32 zone_x = 0; zone_x < c.zone.count.x; ++zone_x) {
        for (u32 zone_y = 0; zone_y < c.zone.count.y; ++zone_y) {
            rect2i32 rect = get_zone_rect(server, workarea, {zone_x, zone_y});
            vec2f64 leeway = vec_cast<f64>(c.zone.selection_leeway * num_cast<f32>(std::min(rect.extent.x, rect.extent.y)));
            aabb2f64 aabb = aabb_cast<f64>(rect);
            aabb2f64 check_aabb = {
                aabb.min - leeway,
                aabb.max + leeway,
                minmax,
            };
            if (aabb_contains(check_aabb, vec_cast<f64>(point))) {
                pointer_zone = any_zones ? aabb_outer(pointer_zone, aabb) : aabb;
                any_zones = true;
            }
        }
    }

    if (any_zones) {
        if (server->zone.selecting) {
            server->zone.final_zone = aabb_outer(server->zone.initial_zone, pointer_zone);
        } else {
            server->zone.final_zone = server->zone.initial_zone = pointer_zone;
        }
    } else {
        server->zone.final_zone = {};
    }

    update_rectangle(server);
}

static
void toggle_selecting(WmServer* server)
{
    server->zone.selecting = !server->zone.selecting;
    zone_update_regions(server);
    wm_cursor_visual_update(server);
}

static
void begin_zone(WmServer* server, SeatPointer* pointer)
{
    auto window = wm_find_window_at(server, seat_pointer_get_position(pointer));
    if (!window || !wm_window_is_resizable(window)) return;

    server->zone.pointer = pointer;
    server->zone.window = window;
    server->zone.selecting = false;

    zone_update_regions(server);
    wm_interaction_set_mode(server, WmInteractionMode::zone);
}

static
void end_zone(WmServer* server)
{
    server->zone.pointer = nullptr;
    update_rectangle(server);

    wm_interaction_set_mode(server, WmInteractionMode::none);

    if (!server->zone.selecting) return;

    if (auto* window = server->zone.window.get()) {
        wm_window_request_reposition(window, rect_cast<f32>(server->zone.final_zone), {1, 1});
        wm_focus(server, window);
    }
}

static
auto filter_event_zone(WmServer* server, SeatEvent* event) -> SeatEventFilterResult
{
    switch (event->type) {
        break;case SeatEventType::pointer_motion:
            if (event->pointer.pointer == server->zone.pointer) zone_update_regions(server);
        break;case SeatEventType::pointer_button:
            if (event->pointer.pointer == server->zone.pointer) {
                if (event->pointer.button.pressed) {
                    if (event->pointer.button.code == BTN_RIGHT) {
                        toggle_selecting(server);
                    }
                    return SeatEventFilterResult::capture;
                }
                if (seat_pointer_get_pressed(server->zone.pointer).empty()) {
                    end_zone(server);
                }
            }
        break;case SeatEventType::pointer_scroll:
            if (event->pointer.pointer == server->zone.pointer) return SeatEventFilterResult::capture;
        break;default:
            ;
    }

    return {};
}

static
auto filter_event_default(WmServer* server, SeatEvent* event) -> SeatEventFilterResult
{
    if (event->type != SeatEventType::pointer_button) return {};

    auto button = event->pointer.button;
    if (!button.pressed) return {};

    if (button.code != BTN_LEFT) return {};

    auto mods = seat_get_modifiers(seat_pointer_get_seat(event->pointer.pointer));
    if (!mods.contains(server->main_mod)) return {};
    if (mods.contains(SeatModifier::shift)) return {}; // Avoid conflicts with movesize interaction

    begin_zone(server, event->pointer.pointer);
    return SeatEventFilterResult::capture;
}

static
auto filter_event(WmServer* server, SeatEvent* event) -> SeatEventFilterResult
{
    switch (server->mode) {
        break;case WmInteractionMode::none:
            return filter_event_default(server, event);
        break;case WmInteractionMode::zone:
            return filter_event_zone(server, event);
        break;default:
            ;
    }

    return SeatEventFilterResult::passthrough;
}

// -----------------------------------------------------------------------------

void wm_init_zone(WmServer* server)
{
    server->zone.texture = scene_texture_create();
    server->zone.filter = seat_add_event_filter(wm_get_seat(server), [server](SeatEvent* event) {
        return filter_event(server, event);
    });
}
