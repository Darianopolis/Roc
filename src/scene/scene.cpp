#include "internal.hpp"

Scene::~Scene()
{
    debug_assert(outputs.empty());
    debug_assert(clients.empty());
    debug_assert(windows.empty());
}

void scene_push_io_event(Scene* scene, IoEvent* event)
{
    switch (event->type) {
        break;case IoEventType::shutdown_requested:
            ;

        break;case IoEventType::input_added:
            scene_handle_input_added(scene_get_exclusive_seat(scene), event->input.device);
        break;case IoEventType::input_removed:
            scene_handle_input_removed(scene_get_exclusive_seat(scene), event->input.device);
        break;case IoEventType::input_event:
            scene_handle_input(scene_get_exclusive_seat(scene), event->input);

        break;case IoEventType::output_configure:
              case IoEventType::output_frame:
              case IoEventType::output_added:
              case IoEventType::output_removed:
            ;
    }
}

auto scene_create(ExecContext* exec, Gpu* gpu) -> Ref<Scene>
{
    auto scene = ref_create<Scene>();

    scene->exec = exec;
    scene->gpu = gpu;

    scene->window_system = scene_register_system(scene.get());

    scene->root_tree = scene_tree_create(scene.get());

    for (auto layer : magic_enum::enum_values<SceneLayer>()) {
        auto* tree = (scene->layers[layer] = scene_tree_create(scene.get())).get();
        scene_tree_place_above(scene->root_tree.get(), nullptr, tree);
    }

    scene_render_init(scene.get());

    scene_cursor_manager_init(scene.get());

    scene_seat_init(scene.get());

    return scene;
}

auto scene_get_layer(Scene* scene, SceneLayer layer) -> SceneTree*
{
    return scene->layers[layer].get();
}

void scene_request_frame(Scene* scene)
{
    for (auto* output : scene->outputs) {
        scene_output_request_frame(output);
    }
}

void scene_broadcast_event(Scene* scene, SceneEvent* event)
{
    for (auto* client : scene->clients) {
        scene_client_post_event(client, event);
    }
}

// -----------------------------------------------------------------------------

auto scene_register_system(Scene* scene) -> SceneSystemId
{
    scene->prev_system_id = SceneSystemId(std::to_underlying(scene->prev_system_id) + 1);
    return scene->prev_system_id;
}
