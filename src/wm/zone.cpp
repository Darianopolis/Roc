#include "internal.hpp"

#include "core/math.hpp"

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

    vec4f32 color_initial = {0.6, 0.6, 0.6, 0.4};
    vec4f32 color_selected = {0.4, 0.4, 1.0, 0.6};
} c;

static
rect2i32 get_zone_rect(rect2i32 workarea, vec2i32 zone)
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
void update_rectangle(WindowManager* wm)
{
    bool show = wm->zone.pointer;
    bool selecting = wm->zone.selecting;
    auto rect = wm->zone.final_zone;

    if (!show) {
        scene_node_unparent(wm->zone.texture.get());
        return;
    }

    auto color = selecting ? c.color_selected : c.color_initial;

    scene_tree_place_above(scene_get_layer(wm->scene, SceneLayer::overlay), nullptr, wm->zone.texture.get());
    scene_texture_set_dst(wm->zone.texture.get(), rect);
    scene_texture_set_tint(wm->zone.texture.get(), color * 255.f);
}

static
void zone_update_regions(WindowManager* wm)
{
    auto pointer = wm->zone.pointer;
    vec2f64 point = scene_pointer_get_position(pointer);

    auto[output, position] = scene_find_output_for_point(wm->scene, point);

    // TODO: Separate "workarea" concept per output
    aabb2i32 workarea = scene_output_get_viewport(output);
    workarea.min += vec2i32{c.external_padding.left, c.external_padding.top};
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
bool is_interactable(SceneWindow* window)
{
    return true;
}

static
void toggle_selecting(WindowManager* wm)
{
    wm->zone.selecting = !wm->zone.selecting;
    zone_update_regions(wm);
}

static
void handle_hotkey(WindowManager* wm, SceneHotkeyEvent event)
{
    auto* pointer = scene_input_device_get_pointer(event.input_device);

    if (!event.pressed) return;

    if (event.hotkey.code == BTN_RIGHT) {
        toggle_selecting(wm);
        return;
    }

    wm->mode = WmInteractionMode::zone;

    scene_pointer_focus(pointer, wm->focus.get());
    wm->zone.pointer = pointer;

    auto window = scene_find_window_at(wm->scene, scene_pointer_get_position(pointer));
    if (window) {
        wm->zone.window = window;
        if (is_interactable(window)) {
            wm->zone.selecting = false;
            zone_update_regions(wm);
        }
    }
}

static
void handle_leave(WindowManager* wm)
{
    wm->zone.pointer = nullptr;
    update_rectangle(wm);

    if (wm->mode != WmInteractionMode::zone) return;
    wm->mode = WmInteractionMode::none;

    if (!wm->zone.selecting) return;

    if (auto* window = wm->zone.window.get()) {
        scene_window_request_reposition(window, wm->zone.final_zone, {1, 1});
    }
}

static
void handle_button(WindowManager* wm, auto button)
{
    if (button.code == BTN_RIGHT && button.pressed) {
        toggle_selecting(wm);
    }
}

void wm_zone_handle_event(WindowManager* wm, SceneEvent* event)
{
    switch (event->type) {
        break;case SceneEventType::hotkey:
            handle_hotkey(wm, event->hotkey);
        break;case SceneEventType::pointer_button:
            handle_button(wm, event->pointer.button);
        break;case SceneEventType::pointer_leave:
            handle_leave(wm);
        break;case SceneEventType::pointer_motion:
            zone_update_regions(wm);
        break;default:
            ;
    }
}

// -----------------------------------------------------------------------------

void wm_init_zone(WindowManager* wm)
{
    wm->zone.texture = scene_texture_create(wm->scene);
}
