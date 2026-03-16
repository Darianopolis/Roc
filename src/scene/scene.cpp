#include "internal.hpp"

scene_context::~scene_context()
{
    core_assert(outputs.empty());
    core_assert(clients.empty());
    core_assert(windows.empty());
}

void scene_push_io_event(scene_context* ctx, io_event* event)
{
    switch (event->type) {
        break;case io_event_type::shutdown_requested:
            ;

        break;case io_event_type::input_added:
            scene_handle_input_added(ctx, event->input.device);
        break;case io_event_type::input_removed:
            scene_handle_input_removed(ctx, event->input.device);
        break;case io_event_type::input_event:
            scene_handle_input(ctx, event->input);

        break;case io_event_type::output_configure:
              case io_event_type::output_frame:
              case io_event_type::output_added:
              case io_event_type::output_removed:
            ;
    }
}

auto scene_create(gpu_context* gpu, core_event_loop* event_loop, io_context* io) -> ref<scene_context>
{
    auto scene = core_create<scene_context>();

    scene->gpu = gpu;
    scene->event_loop = event_loop;

    scene->window_system = scene_register_system(scene.get());

    scene->root_tree = scene_tree_create(scene.get());

    for (auto layer : magic_enum::enum_values<scene_layer>()) {
        auto* tree = (scene->layers[layer] = scene_tree_create(scene.get())).get();
        scene_tree_place_above(scene->root_tree.get(), nullptr, tree);
    }

    scene_render_init(scene.get());

    scene_cursor_manager_init(scene.get());

    scene->seat.keyboard = scene_keyboard_create(scene.get());
    scene->seat.pointer = scene_pointer_create(scene.get());

    return scene;
}

auto scene_get_layer(scene_context* ctx, scene_layer layer) -> scene_tree*
{
    return ctx->layers[layer].get();
}

void scene_request_frame(scene_context* ctx)
{
    for (auto* output : ctx->outputs) {
        scene_output_request_frame(output);
    }
}

void scene_broadcast_event(scene_context* ctx, scene_event* event)
{
    for (auto* client : ctx->clients) {
        scene_client_post_event(client, event);
    }
}

// -----------------------------------------------------------------------------

auto scene_register_system(scene_context* ctx) -> scene_system_id
{
    ctx->prev_system_id = scene_system_id(std::to_underlying(ctx->prev_system_id) + 1);
    return ctx->prev_system_id;
}

// -----------------------------------------------------------------------------

auto scene_get_pointer(scene_context* ctx) -> scene_pointer*
{
    return ctx->seat.pointer.get();
}

auto scene_get_keyboard(scene_context* ctx) -> scene_keyboard*
{
    return ctx->seat.keyboard.get();
}
