#include "internal.hpp"

#include "core/math.hpp"

scene_window::~scene_window()
{
    tree->userdata = nullptr;
    scene_window_unmap(this);
    std::erase(client->ctx->windows, this);
}

auto scene_window_create(scene_client* client) -> ref<scene_window>
{
    auto window = core_create<scene_window>();
    window->client = client;

    auto* ctx = client->ctx;

    ctx->windows.emplace_back(window.get());

    window->tree = scene_tree_create(ctx);

    window->tree->system = client->ctx->window_system;
    window->tree->userdata = window.get();

    return window;
}

auto scene_window_get_tree(scene_window* window) -> scene_tree*
{
    return window->tree.get();
}

void scene_window_set_title(scene_window* window, std::string_view title)
{
    window->title = title;
}

void scene_window_request_reposition(scene_window* window, rect2f32 frame, vec2f32 gravity)
{
    scene_client_post_event(window->client, ptr_to(scene_event {
        .type = scene_event_type::window_reposition,
        .window = {
            .window = window,
            .reposition = {
                .frame = frame,
                .gravity = gravity,
            },
        }
    }));
}

void scene_window_set_frame(scene_window* window, rect2f32 frame)
{
    window->extent = frame.extent;
    scene_tree_set_translation(window->tree.get(), frame.origin);
}

auto scene_window_get_frame(scene_window* window) -> rect2f32
{
    return {
        scene_tree_get_position(window->tree.get()),
        window->extent,
        core_xywh
    };
}

void scene_window_map(scene_window* window)
{
    if (window->mapped) return;

    scene_tree_place_above(scene_get_layer(window->client->ctx, scene_layer::window), nullptr, window->tree.get());

    window->mapped = true;
}

void scene_window_raise(scene_window* window)
{
    if (!window->mapped) return;

    scene_tree_place_above(scene_get_layer(window->client->ctx, scene_layer::window), nullptr, window->tree.get());
}

void scene_window_unmap(scene_window* window)
{
    if (!window->mapped) return;

    scene_node_unparent(window->tree.get());

    window->mapped = false;
}

// -----------------------------------------------------------------------------

auto scene_find_window_at(scene_context* ctx, vec2f32 point) -> scene_window*
{
    // TODO: This will ignore any `input_plane`s currently.
    //       Should we provide (optional) mappings from `input_plane` back to windows
    //       and then intersect against both `input_plane`s and `window` frames?

    scene_window* window = nullptr;

    scene_iterate(ctx->root_tree.get(),
        scene_iterate_direction::front_to_back,
        scene_iterate_default,
        scene_iterate_default,
        [&](scene_tree* tree) {
            if (tree->system == ctx->window_system) {
                auto w = static_cast<scene_window*>(tree->userdata);
                if (core_rect_contains(scene_window_get_frame(w), point)) {
                    window = w;
                    return scene_iterate_action::stop;
                }
            }
            return scene_iterate_action::next;
        });

    return window;
}
