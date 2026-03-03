#include "internal.hpp"

CORE_OBJECT_EXPLICIT_DEFINE(scene_output);

scene_output::~scene_output()
{
    auto* ctx = client->ctx;
    scene_broadcast_event(ctx, ptr_to(scene_event { .type = scene_event_type::output_removed, .output = this }));
    std::erase(ctx->outputs, this);
    scene_broadcast_event(ctx, ptr_to(scene_event { .type = scene_event_type::output_layout }));
}

auto scene_output_create(scene_client* client) -> ref<scene_output>
{
    auto* ctx = client->ctx;
    auto output = core_create<scene_output>();
    output->client = client;
    ctx->outputs.push_back(output.get());
    scene_broadcast_event(ctx, ptr_to(scene_event { .type = scene_event_type::output_added, .output = output.get() }));
    return output;
}

void scene_output_damage(scene_output* output)
{
    scene_client_post_event(output->client, ptr_to(scene_event {
        .type = scene_event_type::output_damaged,
        .output = output,
    }));
}

void scene_output_set_viewport(scene_output* output, rect2f32 viewport)
{
    if (output->viewport == viewport) return;
    auto* ctx = output->client->ctx;
    output->viewport = viewport;
    scene_broadcast_event(ctx, ptr_to(scene_event { .type = scene_event_type::output_configured, .output = output }));
    scene_broadcast_event(ctx, ptr_to(scene_event { .type = scene_event_type::output_layout }));
    scene_output_damage(output);
}

auto scene_list_outputs(scene_context* ctx) -> std::span<scene_output* const>
{
    return ctx->outputs;
}

auto scene_output_get_viewport(scene_output* out) -> rect2f32
{
    return out->viewport;
}

auto scene_find_output_for_point(scene_context* ctx, vec2f32 point) -> scene_find_output_result
{
    vec2f32       best_position = point;
    f32           best_distance = INFINITY;
    scene_output* best_output   = nullptr;
    for (auto* output : scene_list_outputs(ctx)) {
        auto clamped = core_rect_clamp_point(scene_output_get_viewport(output), point);
        if (point == clamped) {
            best_position = point;
            best_output = output;
            break;
        } else if (f32 dist = glm::distance(clamped, point); dist < best_distance) {
            best_position = clamped;
            best_distance = dist;
            best_output = output;
        }
    }
    return { best_output, best_position };
}
