#include "internal.hpp"

#include "core/math.hpp"

SceneWindow::~SceneWindow()
{
    tree->userdata = nullptr;
    scene_window_unmap(this);
    std::erase(client->scene->windows, this);
}

auto scene_window_create(SceneClient* client) -> Ref<SceneWindow>
{
    auto window = ref_create<SceneWindow>();
    window->client = client;

    auto* scene = client->scene;

    scene->windows.emplace_back(window.get());

    window->tree = scene_tree_create(scene);

    window->tree->system = client->scene->window_system;
    window->tree->userdata = window.get();

    return window;
}

auto scene_window_get_tree(SceneWindow* window) -> SceneTree*
{
    return window->tree.get();
}

void scene_window_set_title(SceneWindow* window, std::string_view title)
{
    window->title = title;
}

void scene_window_request_reposition(SceneWindow* window, rect2f32 frame, vec2f32 gravity)
{
    scene_client_post_event(window->client, ptr_to(SceneEvent {
        .type = SceneEventType::window_reposition,
        .window = {
            .window = window,
            .reposition = {
                .frame = frame,
                .gravity = gravity,
            },
        }
    }));
}

void scene_window_request_close(SceneWindow* window)
{
    scene_client_post_event(window->client, ptr_to(SceneEvent {
        .type = SceneEventType::window_close,
        .window = {
            .window = window,
        }
    }));
}

void scene_window_set_frame(SceneWindow* window, rect2f32 frame)
{
    window->extent = frame.extent;
    scene_tree_set_translation(window->tree.get(), frame.origin);
}

auto scene_window_get_frame(SceneWindow* window) -> rect2f32
{
    return {
        scene_tree_get_position(window->tree.get()),
        window->extent,
        xywh
    };
}

void scene_window_map(SceneWindow* window)
{
    if (window->mapped) return;

    scene_tree_place_above(scene_get_layer(window->client->scene, SceneLayer::window), nullptr, window->tree.get());

    window->mapped = true;
}

void scene_window_raise(SceneWindow* window)
{
    if (!window->mapped) return;

    scene_tree_place_above(scene_get_layer(window->client->scene, SceneLayer::window), nullptr, window->tree.get());
}

void scene_window_unmap(SceneWindow* window)
{
    if (!window->mapped) return;

    scene_node_unparent(window->tree.get());

    window->mapped = false;
}

// -----------------------------------------------------------------------------

auto scene_find_window_at(Scene* scene, vec2f32 point) -> SceneWindow*
{
    // TODO: This will ignore any `input_plane`s currently.
    //       Should we provide (optional) mappings from `input_plane` back to windows
    //       and then intersect against both `input_plane`s and `window` frames?

    SceneWindow* window = nullptr;

    scene_iterate<SceneIterateDirection::front_to_back>(scene->root_tree.get(),
        scene_iterate_default,
        scene_iterate_default,
        [&](SceneTree* tree) {
            if (tree->system == scene->window_system) {
                auto w = static_cast<SceneWindow*>(tree->userdata);
                if (rect_contains(scene_window_get_frame(w), point)) {
                    window = w;
                    return SceneIterateAction::stop;
                }
            }
            return SceneIterateAction::next;
        });

    return window;
}
