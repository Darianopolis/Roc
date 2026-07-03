#include "internal.hpp"

#include <core/math.hpp>

void wm_pointer_constraints_init(WmServer* server)
{
    server->pointer_constraints_filter = seat_add_event_filter(wm_get_seat(server), [server](SeatEvent* event) -> SeatEventFilterResult {
        if (event->type == SeatEventType::keyboard_enter || event->type == SeatEventType::keyboard_leave) {
            wm_update_active_pointer_constraint(server);
        }
        return SeatEventFilterResult::passthrough;
    });
}

auto wm_constrain_pointer(WmWindow* window, SceneInputRegion* input_region, Region<f32> region, WmPointerConstraintType type) -> Ref<WmPointerConstraint>
{
    auto constraint = ref_create<WmPointerConstraint>();
    constraint->server = window->client->server;
    constraint->window = window;
    constraint->input_region = input_region;
    constraint->type = type;

    auto* server = window->client->server;
    server->pointer_constraints.insert(server->pointer_constraints.begin(), constraint.get());

    wm_pointer_constraint_set_region(constraint.get(), std::move(region));
    return constraint;
}

void wm_pointer_constraint_set_region(WmPointerConstraint* constraint, Region<f32> region)
{
    constraint->region = std::move(region);
}

WmPointerConstraint::~WmPointerConstraint()
{
    std::erase(server->pointer_constraints, this);
    if (server->active_pointer_constraint == this) {
        wm_update_active_pointer_constraint(server);
    }
}

auto wm_pointer_constraint_apply(WmServer* server, vec2f32 position, vec2f32 delta) -> vec2f32
{
    wm_update_active_pointer_constraint(server);

    if (!server->active_pointer_constraint || server->mode != WmInteractionMode::none) {
        return position + delta;
    }

    auto* input_region = server->active_pointer_constraint->input_region.get();

    switch (server->active_pointer_constraint->type) {
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
    position = server->active_pointer_constraint->region.constrain(position);

    return position + offset;
}

void wm_update_active_pointer_constraint(WmServer* server)
{
    WmPointerConstraint* new_active = nullptr;
    for (auto* constraint : server->pointer_constraints) {
        if (!constraint->input_region || !constraint->input_region->parent) continue;
        if (!constraint->window || !wm_window_is_focused(constraint->window.get())) continue;

        new_active = constraint;
        break;
    }

    if (server->active_pointer_constraint == new_active) return;

    if (server->active_pointer_constraint) {
        for (auto* client : server->clients) {
            wm_client_post_event(client, ptr_to(WmEvent {
                .pointer_constraint = {
                    .type = WmEventType::pointer_constraint_disabled,
                    .constraint = server->active_pointer_constraint,
                }
            }));
        }
    }

    server->active_pointer_constraint = new_active;

    if (new_active) {
        for (auto* client : server->clients) {
            wm_client_post_event(client, ptr_to(WmEvent {
                .pointer_constraint = {
                    .type = WmEventType::pointer_constraint_enabled,
                    .constraint = new_active,
                }
            }));
        }
    }
}
