#include "internal.hpp"

#include "render.h"
#include "scene_render_shader.hpp"

void scene_render_init(scene_context* ctx)
{
    ctx->render.vertex   = gpu_shader_create(ctx->gpu, VK_SHADER_STAGE_VERTEX_BIT,   scene_render_shader, "vertex");
    ctx->render.fragment = gpu_shader_create(ctx->gpu, VK_SHADER_STAGE_FRAGMENT_BIT, scene_render_shader, "fragment");

    ctx->render.white = gpu_image_create(ctx->gpu, {1, 1},
        gpu_format_from_drm(DRM_FORMAT_ABGR8888),
        gpu_image_usage::texture | gpu_image_usage::transfer_dst);
    gpu_image_update_immed(ctx->render.white.get(), ptr_to(vec4u8{255, 255, 255, 255}));

    ctx->render.sampler = gpu_sampler_create(ctx->gpu, VK_FILTER_NEAREST, VK_FILTER_LINEAR);
}

void scene_frame(scene_context* ctx, scene_output* output, io_output* io_output)
{
    scene_broadcast_event(ctx, ptr_to(scene_event {
        .type = scene_event_type::output_frame,
        .redraw = { .output = output },
    }));

    // TODO: Only redraw with damage

    auto target = io_output->acquire(gpu_image_usage::render);
    auto done = scene_render(ctx, target.get(), output->viewport);
    io_output->present(target.get(), done);
}

auto scene_render(scene_context* ctx, gpu_image* target, rect2f32 viewport) -> gpu_syncpoint
{
    auto& render = ctx->render;

    struct scene_draw
    {
        u32 first_vertex;
        u32 first_index;
        u32 num_indices;
        aabb2f32 clip;
        gpu_image* image;
        gpu_sampler* sampler;
        gpu_blend_mode blend;
        scene_transform_state transform;
    };

    std::vector<scene_vertex> vertices;
    std::vector<u32> indices;
    std::vector<scene_draw> draws;

    aabb2f32 default_clip = viewport;

    auto get_draw = [&draws, &vertices, &indices](
        aabb2f32 clip, gpu_image* image, gpu_sampler* sampler, gpu_blend_mode blend, scene_transform_state transform)
    {
        auto* draw = draws.empty() ? nullptr : &draws.back();
        if (  !draw
            || draw->clip      != clip
            || draw->image     != image
            || draw->sampler   != sampler
            || draw->blend     != blend
            || draw->transform != transform)
        {
            draw = &draws.emplace_back(scene_draw{
                .first_vertex = u32(vertices.size()),
                .first_index = u32(indices.size()),
                .clip = clip,
                .image = image,
                .sampler = sampler,
                .blend = blend,
                .transform = transform,
            });
        }
        return draw;
    };

    auto visit_node = [&](scene_node* node) -> scene_iterate_action {
        switch (node->type) {
            break;case scene_node_type::mesh: {
                auto* mesh = static_cast<scene_mesh*>(node);

                draws.emplace_back(scene_draw {
                    .first_vertex = u32(vertices.size()),
                    .first_index = u32(indices.size()),
                    .num_indices = u32(mesh->indices.size()),
                    .clip = core_aabb_inner(default_clip, {
                        node->transform->global.translation + mesh->clip.min * node->transform->global.scale,
                        node->transform->global.translation + mesh->clip.max * node->transform->global.scale,
                        core_minmax
                    }),
                    .image = mesh->image.get() ?: render.white.get(),
                    .sampler = mesh->sampler.get() ?: render.sampler.get(),
                    .blend = mesh->blend,
                    .transform = mesh->transform->global,
                });

                vertices.insert_range(vertices.end(), mesh->vertices);
                indices.insert_range(indices.end(), mesh->indices);
            }
            break;case scene_node_type::texture: {
                auto* texture = static_cast<scene_texture*>(node);
                aabb2f32 src = texture->src;
                aabb2f32 dst = texture->dst;

                //  0 ---- 1
                //  | a /  |  a = 0,2,1
                //  |  / b |  b = 1,2,3
                //  2 ---- 3

                auto* draw = get_draw(default_clip,
                    texture->image.get()   ?: render.white.get(),
                    texture->sampler.get() ?: render.sampler.get(),
                    texture->blend,
                    scene_get_root_transform(ctx)->global);

                auto base_vtx = vertices.size() - draw->first_vertex;
                draw->num_indices += 6;
                for (auto idx : {0, 2, 1, 1, 2, 3}) {
                    indices.emplace_back(base_vtx + idx);
                }

                auto push_vtx = [&](vec2f32 pos, vec2f32 uv = {}) {
                    vertices.emplace_back(scene_vertex {
                        .pos = texture->transform->global.to_global(pos),
                        .uv = uv,
                        .color = texture->tint
                    });
                };
                push_vtx({dst.min.x, dst.min.y}, {src.min.x, src.min.y});
                push_vtx({dst.max.x, dst.min.y}, {src.max.x, src.min.y});
                push_vtx({dst.min.x, dst.max.y}, {src.min.x, src.max.y});
                push_vtx({dst.max.x, dst.max.y}, {src.max.x, src.max.y});
            }
            break;default:
                ;
        }
        return scene_iterate_action::next;
    };

    scene_iterate(ctx->root_tree.get(),
        scene_iterate_direction::back_to_front,
        scene_iterate_default,
        visit_node,
        scene_iterate_default);

    auto gpu = ctx->gpu;

    auto make_gpu = [&]<typename T>(std::span<T> data) {
        gpu_array<T> arr{gpu_buffer_create(gpu, data.size_bytes(), {}), 0};
        std::memcpy(arr.host(), data.data(), data.size_bytes());
        return arr;
    };

    auto gpu_vertices = make_gpu(std::span(vertices));
    auto gpu_indices  = make_gpu(std::span(indices));

    auto queue = gpu_get_queue(gpu, gpu_queue_type::graphics);
    auto commands = gpu_commands_begin(queue);
    auto cmd = commands.get();
    gpu_commands_protect_object(cmd, gpu_vertices.buffer.get());
    gpu_commands_protect_object(cmd, gpu_indices.buffer.get());

    // Protect images

    gpu_commands_protect_object(cmd, target);

    gpu_commands_protect_object(cmd, render.white.get());
    for (auto& draw : draws) {
        gpu_commands_protect_object(cmd, draw.image);
    }

    // Record

    VkExtent2D vk_extent = { target->extent.x, target->extent.y };
    vec2f32 target_extent = target->extent;

    gpu_cmd_reset_graphics_state(cmd);
    gpu_cmd_set_viewports(cmd, {{{}, target_extent, core_xywh}});
    gpu_cmd_bind_shaders(cmd, {ctx->render.vertex.get(), ctx->render.fragment.get()});

    gpu->vk.CmdBeginRendering(cmd->buffer, ptr_to(VkRenderingInfo {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = { {}, vk_extent },
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = ptr_to(VkRenderingAttachmentInfo {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = target->view,
            .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = {.color{.float32{0.f, 0.f, 0.f, 1.f}}},
        }),
    }));
    gpu->vk.CmdBindDescriptorSets(cmd->buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, gpu->pipeline_layout, 0, 1, &gpu->set, 0, nullptr);
    gpu->vk.CmdBindIndexBuffer(cmd->buffer, gpu_indices.buffer->buffer, 0, VK_INDEX_TYPE_UINT32);

    for (auto& draw : draws) {
        gpu_cmd_set_blend_state(cmd, {draw.blend});

        rect2f32 scissor = draw.clip;
        scissor.origin -= viewport.origin;
        gpu_cmd_set_scissors(cmd, {scissor});

        auto draw_scale = 2.f / viewport.extent;
        gpu_cmd_push_constants(cmd, 0, sizeof(scene_render_input),
            ptr_to(scene_render_input {
                .vertices = gpu_vertices.device(),
                .scale = draw_scale * draw.transform.scale,
                .offset = (draw.transform.translation - viewport.origin) * draw_scale - 1.f,
                .texture = {draw.image, render.sampler.get()},
            }));

        gpu->vk.CmdDrawIndexed(cmd->buffer, draw.num_indices, 1, draw.first_index, draw.first_vertex, 0);
    }

    gpu->vk.CmdEndRendering(cmd->buffer);

    return gpu_commands_submit(cmd, {});
}
