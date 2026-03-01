#include "internal.hpp"

CORE_OBJECT_EXPLICIT_DEFINE(scene_context);
CORE_OBJECT_EXPLICIT_DEFINE(scene_window);

static
void reflow_outputs(scene_context* ctx)
{
    f32 x = 0;
    for (auto* out : ctx->outputs) {
        out->viewport.origin = {x, 0.f};
        x += f32(out->viewport.extent.x);
    }
}

static
auto find_output(scene_context* ctx, io_output* io) -> scene_output*
{
    auto iter = std::ranges::find_if(ctx->outputs, [&](auto* o) { return o->io == io; });
    return iter == ctx->outputs.end() ? nullptr : *iter;
}

static
void output_added(scene_context* ctx, io_output* io)
{
    auto output = core_create<scene_output>();
    output->ctx = ctx;
    output->io = io;
    output->viewport = {{}, io_output_get_size(io), core_xywh};
    ctx->outputs.push_back(output.get());
    reflow_outputs(ctx);
    scene_broadcast_event(ctx, ptr_to(scene_event { .type = scene_event_type::output_layout }));
    io_output_request_frame(io, ctx->render.usage);
}

static
void output_removed(scene_context* ctx, io_output* io)
{
    if (!ctx->outputs.erase_if([&](auto* o) { return o->io == io; })) return;
    reflow_outputs(ctx);
    scene_broadcast_event(ctx, ptr_to(scene_event { .type = scene_event_type::output_layout }));
}

static
void output_configure(scene_context* ctx, scene_output* output)
{
    output->viewport.extent = io_output_get_size(output->io);
    reflow_outputs(ctx);
    scene_broadcast_event(ctx, ptr_to(scene_event { .type = scene_event_type::output_layout }));
    io_output_request_frame(output->io, ctx->render.usage);
}

static
void output_redraw(scene_context* ctx, scene_output* output, gpu_image* target)
{
    scene_broadcast_event(ctx, ptr_to(scene_event {
        .type = scene_event_type::redraw,
        .redraw = { .output = output },
    }));
    scene_render(ctx, output, target);
}

struct event_handler
{
    scene_context* ctx;
    void operator()(io_event* event) const {
        switch (event->type) {
            break;case io_event_type::shutdown_requested:
                io_stop(event->ctx);

            break;case io_event_type::input_added:
                scene_handle_input_added(ctx, event->input.device);
            break;case io_event_type::input_removed:
                scene_handle_input_removed(ctx, event->input.device);
            break;case io_event_type::input_event:
                scene_handle_input(ctx, event->input);

            break;case io_event_type::output_configure:
                output_configure(ctx, find_output(ctx, event->output.output));
            break;case io_event_type::output_redraw:
                output_redraw(ctx, find_output(ctx, event->output.output), event->output.target);
            break;case io_event_type::output_added:
                output_added(ctx, event->output.output);
            break;case io_event_type::output_removed:
                output_removed(ctx, event->output.output);
        }
    }
};

auto scene_create(gpu_context* gpu, io_context* io) -> ref<scene_context>
{
    auto scene = core_create<scene_context>();

    scene->gpu = gpu;

    scene->io = io;
    io_set_event_handler(io, event_handler{scene.get()});

    scene->root_tree = scene_tree_create(scene.get());

    for (auto layer : magic_enum::enum_values<scene_layer>()) {
        auto* tree = (scene->layers[layer] = scene_tree_create(scene.get())).get();
        scene_node_set_transform(tree, scene->root_transform.get());
        scene_tree_place_above(scene->root_tree.get(), nullptr, tree);
    }

    scene->root_transform = scene_transform_create(scene.get());

    scene_render_init(scene.get());

    scene->keyboard = scene_keyboard_create(scene.get());
    scene->pointer = scene_pointer_create(scene.get());

    return scene;
}

auto scene_get_layer(scene_context* ctx, scene_layer layer) -> scene_tree*
{
    return ctx->layers[layer].get();
}

auto scene_get_root_transform(scene_context* ctx) -> scene_transform*
{
    return ctx->root_transform.get();
}

void scene_request_redraw(scene_context* ctx)
{
    for (auto* output : io_list_outputs(ctx->io)) {
        io_output_request_frame(output, ctx->render.usage);
    }
}

void scene_broadcast_event(scene_context* ctx, scene_event* event)
{
    for (auto* client : ctx->clients) {
        scene_client_post_event(client, event);
    }
}

// -----------------------------------------------------------------------------

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
