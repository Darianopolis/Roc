#include "internal.hpp"

#include <core/math.hpp>
#include <core/color.hpp>

static
void update_rectangle(WmServer* server)
{
    bool show = server->selection.pointer;
    rect2f32 rect = server->selection.rect;

    if (!show) {
        scene_node_unparent(server->selection.texture.get());
        return;
    }

    scene_tree_place_below(wm_get_layer(server, WmLayer::overlay), nullptr, server->selection.texture.get());
    scene_texture_set_dst(server->selection.texture.get(), rect_cast<f32>(rect));
    scene_texture_set_tint(server->selection.texture.get(), server->config.zone.color_initial);
}

static
void selection_update_regions(WmServer* server)
{
    auto pointer = server->selection.pointer;
    vec2f32 point = seat_pointer_get_position(pointer);

    if (server->selection.selecting) {
        server->selection.rect = {
            vec_min(server->selection.initial_point, point),
            vec_max(server->selection.initial_point, point),
            minmax,
        };
    } else {
        server->selection.initial_point = point;
        server->selection.rect = {point, point, minmax};
    }

    update_rectangle(server);
}

static
void toggle_selecting(WmServer* server)
{
    server->selection.selecting = !server->selection.selecting;
    selection_update_regions(server);
    wm_cursor_visual_update(server);
}

void wm_begin_selection(WmServer* server, SeatPointer* pointer, std::move_only_function<void(rect2f32)> callback)
{
    seat_pointer_set_sticky_focus(pointer, true);

    server->selection.pointer = pointer;
    server->selection.selecting = false;
    server->selection.callback = std::move(callback);

    selection_update_regions(server);
    wm_interaction_set_mode(server, WmInteractionMode::selection);
}

static
void end_selection(WmServer* server)
{
    seat_pointer_set_sticky_focus(server->selection.pointer, false);

    server->selection.pointer = nullptr;
    update_rectangle(server);

    wm_interaction_set_mode(server, WmInteractionMode::none);

    if (!server->selection.selecting) return;

    server->selection.callback(server->selection.rect);
}

static
auto filter_event_selection(WmServer* server, SeatEvent* event) -> SeatEventFilterResult
{
    switch (event->type) {
        break;case SeatEventType::pointer_motion:
            if (event->pointer.pointer == server->selection.pointer) {
                selection_update_regions(server);
                return SeatEventFilterResult::capture;
            }
        break;case SeatEventType::pointer_button:
            if (event->pointer.pointer == server->selection.pointer) {
                if (event->pointer.button.pressed) {
                    if (event->pointer.button.code == BTN_LEFT) {
                        toggle_selecting(server);
                    }
                }
                if (seat_pointer_get_pressed(server->selection.pointer).empty()) {
                    end_selection(server);
                }
                if (event->pointer.button.pressed) {
                    return SeatEventFilterResult::capture;
                }
            }
        break;case SeatEventType::pointer_scroll:
            if (event->pointer.pointer == server->selection.pointer) {
                return SeatEventFilterResult::capture;
            }
        break;default:
            ;
    }

    return SeatEventFilterResult::passthrough;
}

static
auto filter_event(WmServer* server, SeatEvent* event) -> SeatEventFilterResult
{
    switch (server->mode) {
        break;case WmInteractionMode::selection:
            return filter_event_selection(server, event);
        break;default:
            return SeatEventFilterResult::passthrough;
    }
}

// -----------------------------------------------------------------------------

void wm_init_selection(WmServer* server)
{
    server->selection.texture = scene_texture_create();
    server->selection.filter = seat_add_event_filter(wm_get_seat(server), [server](SeatEvent* event) {
        return filter_event(server, event);
    });
}
