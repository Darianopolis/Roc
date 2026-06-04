#include "internal.hpp"

#include <core/math.hpp>

auto seat_pointer_create(const SeatPointerCreateInfo& info) -> Ref<SeatPointer>
{
    auto pointer = ref_create<SeatPointer>();
    pointer->cursor_manager = info.cursor_manager;
    pointer->root = info.root;

    pointer->tree = scene_tree_create();
    scene_tree_place_above(info.layer, nullptr, pointer->tree .get());

    return pointer;
}

// -----------------------------------------------------------------------------

static
auto get_local_position(SeatPointer* pointer, SeatFocus* focus) -> vec2f32
{
    if (!focus) return {};
    auto* region = focus->input_region.get();
    return seat_pointer_get_position(pointer) - (scene_tree_get_position(region->parent) + region->clip.origin);
}

void seat_pointer_set_sticky_focus(SeatPointer* pointer, bool locked)
{
    pointer->sticky_focus = locked;
}

void seat_pointer_focus(SeatPointer* pointer, SeatFocus* new_focus)
{
    auto* old_focus = seat_pointer_get_focus(pointer);

    if (old_focus == new_focus) return;

    pointer->focus = new_focus;

    if (old_focus) {
        seat_post_event(pointer->seat, old_focus->client, ptr_to(SeatEvent {
            .pointer = {
                .type = SeatEventType::pointer_leave,
                .pointer = pointer,
                .focus = old_focus,
            },
        }));
    }

    if (new_focus) {
        seat_post_event(pointer->seat, new_focus->client, ptr_to(SeatEvent {
            .pointer = {
                .type = SeatEventType::pointer_enter,
                .pointer = pointer,
                .focus = new_focus,
                .position = get_local_position(pointer, new_focus),
            }
        }));
    }

    if (!new_focus) {
        seat_pointer_set_xcursor(pointer, "default");
    }
}

static
void update_pointer_focus(SeatPointer* pointer)
{
    SeatFocus* new_focus = nullptr;

    if (!seat_pointer_get_pressed(pointer).empty() || pointer->sticky_focus) {
        // Pointer retains old focus while any pointer buttons pressed or sticky focus enabled
        new_focus = seat_pointer_get_focus(pointer);

    } else if (auto* region = scene_find_input_region_at(pointer->root, seat_pointer_get_position(pointer))) {
        new_focus = seat_find_focus_for_input_region(pointer->seat->manager, region);
    }

    seat_pointer_focus(pointer, new_focus);
}

void seat_pointer_button(SeatPointer* pointer, SeatInputCode code, bool pressed, bool quiet)
{
    if (pressed ? pointer->pressed.inc(code) : pointer->pressed.dec(code)) {
        seat_post_input_event(pointer, ptr_to(SeatEvent {
            .pointer = {
                .type = SeatEventType::pointer_button,
                .pointer = pointer,
                .focus = pointer->focus,
                .position = get_local_position(pointer, pointer->focus),
                .button = {
                    .code    = code,
                    .pressed = pressed,
                    .quiet   = quiet,
                },
        }}));

        if (!pressed) {
            if (pointer->drag && seat_pointer_get_pressed(pointer).empty()) {
                seat_pointer_end_drag(pointer);
            }
            update_pointer_focus(pointer);
        }
    }
}

void seat_pointer_move(SeatPointer* pointer, vec2f32 position, vec2f32 rel_accel, vec2f32 rel_unaccel)
{
    bool send_event = pointer->tree->translation != position
                   || rel_accel.x   || rel_accel.y
                   || rel_unaccel.x || rel_unaccel.y;

    scene_tree_set_translation(pointer->tree.get(), position);

    update_pointer_focus(pointer);

    seat_pointer_update_drag(pointer);

    if (!send_event) return;

    seat_post_input_event(pointer, ptr_to(SeatEvent {
        .pointer = {
            .type = SeatEventType::pointer_motion,
            .pointer = pointer,
            .focus = pointer->focus,
            .position = get_local_position(pointer, pointer->focus),
            .motion = {
                .rel_accel   = rel_accel,
                .rel_unaccel = rel_unaccel,
            },
        },
    }));
}

void seat_pointer_scroll(SeatPointer* pointer, vec2f32 delta)
{
    seat_post_input_event(pointer, ptr_to(SeatEvent {
        .pointer = {
            .type = SeatEventType::pointer_scroll,
            .pointer = pointer,
            .focus = pointer->focus,
            .position = get_local_position(pointer, pointer->focus),
            .scroll = {
                .delta = delta,
            }
        },
    }));
}

// -----------------------------------------------------------------------------

auto seat_pointer_get_position(SeatPointer* pointer) -> vec2f32
{
    return scene_tree_get_position(pointer->tree.get());
}

auto seat_pointer_get_pressed(SeatPointer* pointer) -> std::span<const SeatInputCode>
{
    return pointer->pressed;
}

auto seat_pointer_get_focus(SeatPointer* pointer) -> SeatFocus*
{
    return pointer->focus;
}

auto seat_pointer_get_seat(SeatPointer* pointer) -> Seat*
{
    return pointer->seat;
}
