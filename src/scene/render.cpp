#include "internal.hpp"

#include "render.h"
#include "scene_render_shader.hpp"

void scene_render_init(scene_context* ctx)
{
    ctx->render.usage = gpu_image_usage::render;

    ctx->render.postmult = gpu_pipeline_create_graphics(
        ctx->gpu, gpu_blend_mode::postmultiplied,
        gpu_format_from_drm(DRM_FORMAT_ABGR8888),
        scene_render_shader, "vertex", "fragment");

    ctx->render.premult = gpu_pipeline_create_graphics(
        ctx->gpu, gpu_blend_mode::premultiplied,
        gpu_format_from_drm(DRM_FORMAT_ABGR8888),
        scene_render_shader, "vertex", "fragment");

    ctx->render.white = gpu_image_create(ctx->gpu, {1, 1},
        gpu_format_from_drm(DRM_FORMAT_ABGR8888),
        gpu_image_usage::texture | gpu_image_usage::transfer_dst);
    gpu_image_update_immed(ctx->render.white.get(), ptr_to(vec4u8{255, 255, 255, 255}));

    ctx->render.sampler = gpu_sampler_create(ctx->gpu, VK_FILTER_NEAREST, VK_FILTER_LINEAR);
}

auto scene_render(scene_context* ctx, scene_output* output, io_output* io_output) -> gpu_syncpoint
{
    scene_broadcast_event(ctx, ptr_to(scene_event {
        .type = scene_event_type::output_frame,
        .redraw = { .output = output },
    }));

    auto target = io_output->acquire(ctx->render.usage);
    auto done = scene_render(ctx, target.get(), output->viewport);
    io_output->present(target.get(), done);

    return done;
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

    auto visit_node = [&](scene_node* node) -> scene_iterate_action {
        switch (node->type) {
            break;case scene_node_type::mesh: {
                auto* mesh = static_cast<scene_mesh*>(node);

                u32 vtx = vertices.size();
                u32 idx = indices.size();
                vertices.insert_range(vertices.end(), mesh->vertices);
                indices.insert_range(indices.end(), mesh->indices);

                draws.emplace_back(scene_draw {
                    .first_vertex = vtx,
                    .first_index = idx,
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
            }
            break;case scene_node_type::texture: {
                auto* texture = static_cast<scene_texture*>(node);
                aabb2f32 src = texture->src;
                aabb2f32 dst = texture->dst;

                //  0 ---- 1
                //  | a /  |  a = 0,2,1
                //  |  / b |  b = 1,2,3
                //  2 ---- 3

                u32 vtx = vertices.size();
                u32 idx = indices.size();
                auto push_vtx = [&](vec2f32 pos, vec2f32 uv = {}) {
                    vertices.emplace_back(scene_vertex { .pos = pos, .uv = uv, .color = texture->tint });
                };
                push_vtx({dst.min.x, dst.min.y}, {src.min.x, src.min.y});
                push_vtx({dst.max.x, dst.min.y}, {src.max.x, src.min.y});
                push_vtx({dst.min.x, dst.max.y}, {src.min.x, src.max.y});
                push_vtx({dst.max.x, dst.max.y}, {src.max.x, src.max.y});
                indices.append_range(std::array{0, 2, 1, 1, 2, 3});

                draws.emplace_back(scene_draw {
                    .first_vertex = vtx,
                    .first_index = idx,
                    .num_indices = 6,
                    .clip = default_clip,
                    .image = texture->image.get() ?: render.white.get(),
                    .sampler = texture->sampler.get() ?: render.sampler.get(),
                    .blend = texture->blend,
                    .transform = texture->transform->global,
                });
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
    auto cmd = commands->buffer;
    gpu_commands_protect_object(commands.get(), gpu_vertices.buffer.get());
    gpu_commands_protect_object(commands.get(), gpu_indices.buffer.get());

    // Protect images

    gpu_commands_protect_object(commands.get(), target);

    gpu_commands_protect_object(commands.get(), render.white.get());
    for (auto& draw : draws) {
        gpu_commands_protect_object(commands.get(), draw.image);
    }

    // Record

    VkExtent2D vk_extent = { target->extent.x, target->extent.y };
    vec2f32 target_extent = target->extent;

    gpu->vk.CmdBeginRendering(cmd, ptr_to(VkRenderingInfo {
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
    gpu->vk.CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, gpu->pipeline_layout, 0, 1, &gpu->set, 0, nullptr);
    gpu->vk.CmdBindIndexBuffer(cmd, gpu_indices.buffer->buffer, 0, VK_INDEX_TYPE_UINT32);

    gpu->vk.CmdSetViewport(cmd, 0, 1, ptr_to(VkViewport {
        0, 0,
        target_extent.x, target_extent.y,
        0, 1,
    }));

    for (auto& draw : draws) {
        switch (draw.blend) {
            break;case gpu_blend_mode::premultiplied:
                gpu->vk.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, render.premult->pipeline);
            break;case gpu_blend_mode::postmultiplied:
                gpu->vk.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, render.postmult->pipeline);
            break;case gpu_blend_mode::none:
                core_assert_fail("", "Must select blend mode");
        }
        rect2f32 scissor = draw.clip;
        scissor.origin -= viewport.origin;
        gpu->vk.CmdSetScissor(cmd, 0, 1, ptr_to(VkRect2D {
            .offset = {i32(scissor.origin.x), i32(scissor.origin.y)},
            .extent = {u32(scissor.extent.x), u32(scissor.extent.y)},
        }));
        auto draw_scale = 2.f / viewport.extent;
        gpu->vk.CmdPushConstants(cmd, gpu->pipeline_layout, VK_SHADER_STAGE_ALL, 0, sizeof(scene_render_input),
            ptr_to(scene_render_input {
                .vertices = gpu_vertices.device(),
                .scale = draw_scale * draw.transform.scale,
                .offset = (draw.transform.translation - viewport.origin) * draw_scale - 1.f,
                .texture = {draw.image, render.sampler.get()},
            }));
        gpu->vk.CmdDrawIndexed(cmd, draw.num_indices, 1, draw.first_index, draw.first_vertex, 0);
    }

    gpu->vk.CmdEndRendering(cmd);

    return gpu_commands_submit(commands.get(), {});
}
