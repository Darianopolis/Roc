#include "internal.hpp"

#include <core/math.hpp>
#include <core/color.hpp>

static
struct {
    vec2u32 zones = {6, 2};
    vec2f32 selection_leeway = {0.3f, 0.3f};
    i32 padding_inner = 8;
    struct {
        i32 left   = 9;
        i32 top    = 9;
        i32 right  = 9;
        i32 bottom = 9;
    } external_padding;

    vec4u8 color_initial  = color_from_hex("#99999999");
    vec4u8 color_selected = color_from_hex("#6666FF99");
} c;

static
auto get_zone_rect(rect2i32 workarea, vec2i32 zone) -> rect2i32
{
    rect2i32 out;

    auto get_axis = [&](glm::length_t axis) {
        i32 usable_length = workarea.extent[axis] - (c.padding_inner * (c.zones[axis] - 1));
        f64 ideal_zone_size = f64(usable_length) / c.zones[axis];
        out.origin[axis] = std::round(ideal_zone_size *  zone[axis]     );
        out.extent[axis] = std::round(ideal_zone_size * (zone[axis] + 1)) - out.origin[axis];
        out.origin[axis] += workarea.origin[axis] + c.padding_inner * zone[axis];
    };
    get_axis(0);
    get_axis(1);

    return out;
}

static
void update_rectangle(WmServer* wm)
{
    bool show = wm->zone.pointer;
    bool selecting = wm->zone.selecting;
    auto rect = wm->zone.final_zone;

    if (!show) {
        scene_node_unparent(wm->zone.texture.get());
        return;
    }

    auto color = selecting ? c.color_selected : c.color_initial;

    scene_tree_place_above(wm_get_layer(wm, WmLayer::overlay), nullptr, wm->zone.texture.get());
    scene_texture_set_dst(wm->zone.texture.get(), rect);
    scene_texture_set_tint(wm->zone.texture.get(), color);
}

static
void zone_update_regions(WmServer* wm)
{
    auto pointer = wm->zone.pointer;
    vec2f64 point = seat_pointer_get_position(pointer);

    auto[output, position] = wm_find_output_at(wm, point);

    // TODO: Separate "workarea" concept per output
    aabb2i32 workarea = output->viewport;
    workarea.min += vec2i32{c.external_padding.left,  c.external_padding.top};
    workarea.max -= vec2i32{c.external_padding.right, c.external_padding.bottom};

    aabb2f64 pointer_zone = {};
    bool any_zones = false;

    for (u32 zone_x = 0; zone_x < c.zones.x; ++zone_x) {
        for (u32 zone_y = 0; zone_y < c.zones.y; ++zone_y) {
            auto rect = get_zone_rect(workarea, {zone_x, zone_y});
            vec2f64 leeway = c.selection_leeway * f32(std::min(rect.extent.x, rect.extent.y));
            aabb2f64 aabb = rect;
            aabb2f64 check_aabb = {
                aabb.min - leeway,
                aabb.max + leeway,
                minmax,
            };
            if (aabb_contains(check_aabb, point)) {
                pointer_zone = any_zones ? aabb_outer(pointer_zone, aabb) : aabb;
                any_zones = true;
            }
        }
    }

    if (any_zones) {
        if (wm->zone.selecting) {
            wm->zone.final_zone = aabb_outer(wm->zone.initial_zone, pointer_zone);
        } else {
            wm->zone.final_zone = wm->zone.initial_zone = pointer_zone;
        }
    } else {
        wm->zone.final_zone = {};
    }

    update_rectangle(wm);
}

static
auto is_interactable(WmWindow* window) -> bool
{
    return true;
}

static
void toggle_selecting(WmServer* wm)
{
    wm->zone.selecting = !wm->zone.selecting;
    zone_update_regions(wm);
}

static
void begin_zone(WmServer* wm, SeatPointer* pointer)
{
    wm->mode = WmInteractionMode::zone;

    wm->zone.pointer = pointer;

    auto window = wm_find_window_at(wm, seat_pointer_get_position(pointer));
    if (window) {
        wm->zone.window = window;
        if (is_interactable(window)) {
            wm->zone.selecting = false;
            zone_update_regions(wm);
        }
    }
}

static
void end_zone(WmServer* wm)
{
    wm->zone.pointer = nullptr;
    update_rectangle(wm);

    wm->mode = WmInteractionMode::none;

    if (!wm->zone.selecting) return;

    if (auto* window = wm->zone.window.get()) {
        wm_window_request_reposition(window, wm->zone.final_zone, {1, 1});
        wm_window_focus(window);
    }
}

static
auto filter_event_zone(WmServer* wm, SeatEvent* event) -> SeatEventFilterResult
{
    switch (event->type) {
        break;case SeatEventType::pointer_motion:
            if (event->pointer.pointer == wm->zone.pointer) zone_update_regions(wm);
        break;case SeatEventType::pointer_button:
            if (event->pointer.pointer == wm->zone.pointer) {
                if (event->pointer.button.pressed) {
                    if (event->pointer.button.code == BTN_RIGHT) {
                        toggle_selecting(wm);
                    }
                    return SeatEventFilterResult::capture;
                }
                if (seat_pointer_get_pressed(wm->zone.pointer).empty()) {
                    end_zone(wm);
                }
            }
        break;case SeatEventType::pointer_scroll:
            if (event->pointer.pointer == wm->zone.pointer) return SeatEventFilterResult::capture;
        break;default:
            ;
    }

    return {};
}

static
auto filter_event_default(WmServer* wm, SeatEvent* event) -> SeatEventFilterResult
{
    if (event->type != SeatEventType::pointer_button) return {};

    auto button = event->pointer.button;
    if (!button.pressed) return {};

    if (button.code != BTN_LEFT) return {};

    auto mods = seat_get_modifiers(seat_pointer_get_seat(event->pointer.pointer));
    if (!mods.contains(wm->main_mod)) return {};
    if (mods.contains(SeatModifier::shift)) return {}; // Avoid conflicts with movesize interaction

    begin_zone(wm, event->pointer.pointer);
    return SeatEventFilterResult::capture;
}

static
auto filter_event(WmServer* wm, SeatEvent* event) -> SeatEventFilterResult
{
    switch (wm->mode) {
        break;case WmInteractionMode::none:
            return filter_event_default(wm, event);
        break;case WmInteractionMode::zone:
            return filter_event_zone(wm, event);
        break;default:
            ;
    }

    return SeatEventFilterResult::passthrough;
}

// -----------------------------------------------------------------------------

void wm_init_zone(WmServer* wm)
{
    wm->zone.texture = scene_texture_create();
    wm->zone.filter = seat_add_event_filter(wm_get_seat(wm), [wm](SeatEvent* event) {
        return filter_event(wm, event);
    });
}
