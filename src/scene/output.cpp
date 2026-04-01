#include "internal.hpp"

#include "core/math.hpp"

SceneOutput::~SceneOutput()
{
    auto* scene = client->scene;
    scene_broadcast_event(scene, ptr_to(SceneEvent { .type = SceneEventType::output_removed, .output = this }));
    std::erase(scene->outputs, this);
    scene_broadcast_event(scene, ptr_to(SceneEvent { .type = SceneEventType::output_layout }));
}

auto scene_output_create(SceneClient* client, Flags<SceneOutputFlag> flags) -> Ref<SceneOutput>
{
    auto* scene = client->scene;
    auto output = ref_create<SceneOutput>();
    output->client = client;
    output->flags = flags;
    scene->outputs.push_back(output.get());
    scene_broadcast_event(scene, ptr_to(SceneEvent { .type = SceneEventType::output_added, .output = output.get() }));
    return output;
}

void scene_output_request_frame(SceneOutput* output)
{
    scene_client_post_event(output->client, ptr_to(SceneEvent {
        .type = SceneEventType::output_frame_request,
        .output = output,
    }));
}

void scene_output_set_viewport(SceneOutput* output, rect2f32 viewport)
{
    if (output->viewport == viewport) return;
    auto* scene = output->client->scene;
    output->viewport = viewport;
    scene_broadcast_event(scene, ptr_to(SceneEvent { .type = SceneEventType::output_configured, .output = output }));
    scene_broadcast_event(scene, ptr_to(SceneEvent { .type = SceneEventType::output_layout }));
    scene_update_pointers(scene);
    scene_output_request_frame(output);
}

auto scene_list_outputs(Scene* scene) -> std::span<SceneOutput* const>
{
    return scene->outputs;
}

auto scene_output_get_viewport(SceneOutput* output) -> rect2f32
{
    return output->viewport;
}

auto scene_find_output_for_point(Scene* scene, vec2f32 point) -> SceneFindOutputResult
{
    vec2f32       best_position = point;
    f32           best_distance = INFINITY;
    SceneOutput* best_output   = nullptr;
    for (auto* output : scene_list_outputs(scene)) {
        if (!output->flags.contains(SceneOutputFlag::workspace)) continue;
        auto clamped = rect_clamp_point(scene_output_get_viewport(output), point);
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
