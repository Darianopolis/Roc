#include "internal.hpp"

#include "core/math.hpp"

#include "render.h"
#include "scene_render_shader.hpp"

void scene_render_init(scene_context* ctx)
{
    ctx->render.vertex   = gpu_shader_create(ctx->gpu, {
        .stage = VK_SHADER_STAGE_VERTEX_BIT,
        .code = scene_render_shader,
        .entry = "vertex"
    });
    ctx->render.fragment = gpu_shader_create(ctx->gpu, {
        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .code = scene_render_shader,
        .entry = "fragment"
    });

    ctx->render.white = gpu_image_create(ctx->gpu, {
        .extent = {1, 1},
        .format = gpu_format_from_drm(DRM_FORMAT_ABGR8888),
        .usage = gpu_image_usage::texture | gpu_image_usage::transfer_dst
    });

    gpu_copy_memory_to_image(ctx->render.white.get(), {ptr_to(vec4u8{255, 255, 255, 255}), 4}, {{{1, 1}}});

    ctx->render.sampler = gpu_sampler_create(ctx->gpu, {
        .mag = VK_FILTER_NEAREST,
        .min = VK_FILTER_LINEAR,
    });
}

void scene_frame(scene_context* ctx, scene_output* output)
{
    scene_broadcast_event(ctx, ptr_to(scene_event {
        .type = scene_event_type::output_frame,
        .redraw = { .output = output },
    }));
}

void scene_render(scene_context* ctx, gpu_image* target, rect2f32 viewport)
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
        vec2f32 position;
    };

    std::vector<scene_vertex> vertices;
    std::vector<u32> indices;
    std::vector<scene_draw> draws;

    aabb2f32 default_clip = viewport;

    auto get_draw = [&draws, &vertices, &indices](
        aabb2f32 clip, gpu_image* image, gpu_sampler* sampler, gpu_blend_mode blend, vec2f32 position)
    {
        auto* draw = draws.empty() ? nullptr : &draws.back();
        if (  !draw
            || draw->clip     != clip
            || draw->image    != image
            || draw->sampler  != sampler
            || draw->blend    != blend
            || draw->position != position)
        {
            draw = &draws.emplace_back(scene_draw{
                .first_vertex = u32(vertices.size()),
                .first_index = u32(indices.size()),
                .clip = clip,
                .image = image,
                .sampler = sampler,
                .blend = blend,
                .position = position,
            });
        }
        return draw;
    };

    auto draw_mesh = [&](scene_mesh* mesh) {
        auto pos = scene_tree_get_position(mesh->parent);

        draws.emplace_back(scene_draw {
            .first_vertex = u32(vertices.size()),
            .first_index = u32(indices.size()),
            .num_indices = u32(mesh->indices.size()),
            .clip = core_aabb_inner(default_clip, {
                pos + mesh->clip.min,
                pos + mesh->clip.max,
                core_minmax
            }),
            .image = mesh->image.get() ?: render.white.get(),
            .sampler = mesh->sampler.get() ?: render.sampler.get(),
            .blend = mesh->blend,
            .position = pos,
        });

        vertices.insert_range(vertices.end(), mesh->vertices);
        indices.insert_range(indices.end(), mesh->indices);
    };

    auto draw_texture = [&](scene_texture* texture) {
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
            {});

        auto base_vtx = vertices.size() - draw->first_vertex;
        draw->num_indices += 6;
        for (auto idx : {0, 2, 1, 1, 2, 3}) {
            indices.emplace_back(base_vtx + idx);
        }

        auto translation = scene_tree_get_position(texture->parent);
        auto push_vtx = [&](vec2f32 pos, vec2f32 uv = {}) {
            vertices.emplace_back(scene_vertex {
                .pos = translation + pos,
                .uv = uv,
                .color = texture->tint
            });
        };
        push_vtx({dst.min.x, dst.min.y}, {src.min.x, src.min.y});
        push_vtx({dst.max.x, dst.min.y}, {src.max.x, src.min.y});
        push_vtx({dst.min.x, dst.max.y}, {src.min.x, src.max.y});
        push_vtx({dst.max.x, dst.max.y}, {src.max.x, src.max.y});
    };

    scene_iterate<scene_iterate_direction::back_to_front>(ctx->root_tree.get(),
        scene_iterate_default,
        core_overload_set {
            draw_mesh,
            draw_texture,
            [](scene_input_region*) {},
        },
        scene_iterate_default);

    auto gpu = ctx->gpu;

    auto make_gpu = [&]<typename T>(std::span<T> data) {
        gpu_array<T> arr{gpu_buffer_create(gpu, data.size_bytes(), {}), 0};
        std::memcpy(arr.host(), data.data(), data.size_bytes());
        return arr;
    };

    auto gpu_vertices = make_gpu(std::span(vertices));
    auto gpu_indices  = make_gpu(std::span(indices));

    gpu_protect(gpu, gpu_vertices.buffer.get());
    gpu_protect(gpu, gpu_indices.buffer.get());

    // Protect images

    gpu_protect(gpu, render.white.get());
    for (auto& draw : draws) {
        gpu_protect(gpu, draw.image);
    }

    // Record

    gpu_render(gpu, {
        .target = target,
        .clear_color = {0,0,0,1},
    }, [&](gpu_renderpass& pass) {
        pass.set_viewports({{{}, target->extent(), core_xywh}});
        pass.bind_shaders({ctx->render.vertex.get(), ctx->render.fragment.get()});
        pass.bind_index_buffer(gpu_indices.buffer.get(), 0, VK_INDEX_TYPE_UINT32);

        for (auto& draw : draws) {
            pass.set_blend_state({draw.blend});

            rect2f32 scissor = draw.clip;
            scissor.origin -= viewport.origin;
            pass.set_scissors({scissor});

            auto draw_scale = 2.f / viewport.extent;
            pass.push_constants(0, core_view_bytes(scene_render_input {
                .vertices = gpu_vertices.device(),
                .scale = draw_scale,
                .offset = (draw.position - viewport.origin) * draw_scale - 1.f,
                .texture = {draw.image, render.sampler.get()},
            }));

            pass.draw_indexed({
                .index_count = draw.num_indices,
                .instance_count = 1,
                .first_index = draw.first_index,
                .vertex_offset = draw.first_vertex,
                .first_instance = 0
            });
        }
    });
}
