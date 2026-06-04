#include "internal.hpp"

#include <core/math.hpp>
#include <core/color.hpp>

static
void update_rectangle(WmServer* wm)
{
    bool show = wm->selection.pointer;
    rect2f32 rect = wm->selection.rect;

    if (!show) {
        scene_node_unparent(wm->selection.texture.get());
        return;
    }

    scene_tree_place_below(wm_get_layer(wm, WmLayer::overlay), nullptr, wm->selection.texture.get());
    scene_texture_set_dst(wm->selection.texture.get(), rect_cast<f32>(rect));
    scene_texture_set_tint(wm->selection.texture.get(), wm_config.zone.color_initial);
}

static
void selection_update_regions(WmServer* wm)
{
    auto pointer = wm->selection.pointer;
    vec2f32 point = seat_pointer_get_position(pointer);

    if (wm->selection.selecting) {
        wm->selection.rect = {
            vec_min(wm->selection.initial_point, point),
            vec_max(wm->selection.initial_point, point),
            minmax,
        };
    } else {
        wm->selection.initial_point = point;
        wm->selection.rect = {point, point, minmax};
    }

    update_rectangle(wm);
}

static
void toggle_selecting(WmServer* wm)
{
    wm->selection.selecting = !wm->selection.selecting;
    selection_update_regions(wm);
    wm_cursor_visual_update(wm);
}

void wm_begin_selection(WmServer* wm, SeatPointer* pointer, std::move_only_function<void(rect2f32)> callback)
{
    seat_pointer_set_sticky_focus(pointer, true);

    wm->selection.pointer = pointer;
    wm->selection.selecting = false;
    wm->selection.callback = std::move(callback);

    selection_update_regions(wm);
    wm_interaction_set_mode(wm, WmInteractionMode::selection);
}

static
void end_selection(WmServer* wm)
{
    seat_pointer_set_sticky_focus(wm->selection.pointer, false);

    wm->selection.pointer = nullptr;
    update_rectangle(wm);

    wm_interaction_set_mode(wm, WmInteractionMode::none);

    if (!wm->selection.selecting) return;

    wm->selection.callback(wm->selection.rect);
}

static
auto filter_event_selection(WmServer* wm, SeatEvent* event) -> SeatEventFilterResult
{
    switch (event->type) {
        break;case SeatEventType::pointer_motion:
            if (event->pointer.pointer == wm->selection.pointer) {
                selection_update_regions(wm);
                return SeatEventFilterResult::capture;
            }
        break;case SeatEventType::pointer_button:
            if (event->pointer.pointer == wm->selection.pointer) {
                if (event->pointer.button.pressed) {
                    if (event->pointer.button.code == BTN_LEFT) {
                        toggle_selecting(wm);
                    }
                }
                if (seat_pointer_get_pressed(wm->selection.pointer).empty()) {
                    end_selection(wm);
                }
                if (event->pointer.button.pressed) {
                    return SeatEventFilterResult::capture;
                }
            }
        break;case SeatEventType::pointer_scroll:
            if (event->pointer.pointer == wm->selection.pointer) {
                return SeatEventFilterResult::capture;
            }
        break;default:
            ;
    }

    return SeatEventFilterResult::passthrough;
}

static
auto filter_event(WmServer* wm, SeatEvent* event) -> SeatEventFilterResult
{
    switch (wm->mode) {
        break;case WmInteractionMode::selection:
            return filter_event_selection(wm, event);
        break;default:
            return SeatEventFilterResult::passthrough;
    }
}

// -----------------------------------------------------------------------------

void wm_init_selection(WmServer* wm)
{
    wm->selection.texture = scene_texture_create();
    wm->selection.filter = seat_add_event_filter(wm_get_seat(wm), [wm](SeatEvent* event) {
        return filter_event(wm, event);
    });
}
