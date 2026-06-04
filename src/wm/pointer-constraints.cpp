#include "internal.hpp"

#include <core/math.hpp>

void wm_pointer_constraints_init(WmServer* wm)
{
    wm->pointer_constraints_filter = seat_add_event_filter(wm_get_seat(wm), [wm](SeatEvent* event) -> SeatEventFilterResult {
        if (event->type == SeatEventType::keyboard_enter || event->type == SeatEventType::keyboard_leave) {
            wm_update_active_pointer_constraint(wm);
        }
        return SeatEventFilterResult::passthrough;
    });
}

auto wm_constrain_pointer(WmWindow* window, SceneInputRegion* input_region, region2f32 region, WmPointerConstraintType type) -> Ref<WmPointerConstraint>
{
    auto constraint = ref_create<WmPointerConstraint>();
    constraint->wm = window->client->wm;
    constraint->window = window;
    constraint->input_region = input_region;
    constraint->type = type;

    auto* wm = window->client->wm;
    wm->pointer_constraints.insert(wm->pointer_constraints.begin(), constraint.get());

    wm_pointer_constraint_set_region(constraint.get(), region);
    return constraint;
}

void wm_pointer_constraint_set_region(WmPointerConstraint* constraint, region2f32 region)
{
    constraint->region = region;
}

WmPointerConstraint::~WmPointerConstraint()
{
    std::erase(wm->pointer_constraints, this);
    if (wm->active_pointer_constraint == this) {
        wm_update_active_pointer_constraint(wm);
    }
}

auto wm_pointer_constraint_apply(WmServer* wm, vec2f32 position, vec2f32 delta) -> vec2f32
{
    wm_update_active_pointer_constraint(wm);

    if (!wm->active_pointer_constraint || wm->mode != WmInteractionMode::none) {
        return position + delta;
    }

    auto* input_region = wm->active_pointer_constraint->input_region.get();

    switch (wm->active_pointer_constraint->type) {
        break;case WmPointerConstraintType::locked:
            ;
        break;case WmPointerConstraintType::confined:
            position += delta;
    }

    auto offset = scene_tree_get_position(input_region->parent);

    position -= offset;

    // TODO: Force pointer focus while applying constraint
    rect2f32 clip = input_region->clip;
    clip.extent -= 1.f;

    position = rect_clamp_point(clip, position);
    position = input_region->region.constrain(position);
    position = wm->active_pointer_constraint->region.constrain(position);

    return position + offset;
}

void wm_update_active_pointer_constraint(WmServer* wm)
{
    WmPointerConstraint* new_active = nullptr;
    for (auto* constraint : wm->pointer_constraints) {
        if (!constraint->input_region || !constraint->input_region->parent) continue;
        if (!constraint->window || !wm_window_is_focused(constraint->window.get())) continue;

        new_active = constraint;
        break;
    }

    if (wm->active_pointer_constraint == new_active) return;

    if (wm->active_pointer_constraint) {
        for (auto* client : wm->clients) {
            wm_client_post_event(client, ptr_to(WmEvent {
                .pointer_constraint = {
                    .type = WmEventType::pointer_constraint_disabled,
                    .constraint = wm->active_pointer_constraint,
                }
            }));
        }
    }

    wm->active_pointer_constraint = new_active;

    if (new_active) {
        for (auto* client : wm->clients) {
            wm_client_post_event(client, ptr_to(WmEvent {
                .pointer_constraint = {
                    .type = WmEventType::pointer_constraint_enabled,
                    .constraint = new_active,
                }
            }));
        }
    }
}
