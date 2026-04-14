#include "internal.hpp"

#include "core/math.hpp"

#include "scene_render_vert.hpp"
#include "scene_render_frag.hpp"

void scene_render_init(Scene* scene)
{
    scene->render.vertex   = gpu_shader_create(scene->gpu, {
        .stage = VK_SHADER_STAGE_VERTEX_BIT,
        .code  = scene_render_vert,
        .entry = "main",
    });
    scene->render.fragment = gpu_shader_create(scene->gpu, {
        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .code  = scene_render_frag,
        .entry = "main",
    });

    scene->render.white = gpu_image_create(scene->gpu, {
        .extent = {1, 1},
        .format = gpu_format_from_drm(DRM_FORMAT_ABGR8888),
        .usage = GpuImageUsage::texture | GpuImageUsage::transfer_dst
    });

    gpu_copy_memory_to_image(scene->render.white.get(), as_bytes(ptr_to(vec4u8{255, 255, 255, 255}), 4), {{{1, 1}}});

    scene->render.sampler = gpu_sampler_create(scene->gpu, {
        .mag = VK_FILTER_NEAREST,
        .min = VK_FILTER_LINEAR,
    });
}

void scene_render(Scene* scene, GpuImage* target, rect2f32 viewport)
{
    auto& render = scene->render;

    struct Draw
    {
        u32 vertex_offset;
        u32 first_index;
        u32 index_count;
        aabb2f32 clip;
        GpuImage* image;
        GpuSampler* sampler;
        GpuBlendMode blend;
        vec2f32 position;
    };

    std::vector<SceneVertex> vertices;
    std::vector<u32> indices;
    std::vector<Draw> draws;

    aabb2f32 default_clip = viewport;

    auto get_draw = [&draws, &vertices, &indices](
        aabb2f32 clip, GpuImage* image, GpuSampler* sampler, GpuBlendMode blend, vec2f32 position)
    {
        auto* draw = draws.empty() ? nullptr : &draws.back();
        if (  !draw
            || draw->clip     != clip
            || draw->image    != image
            || draw->sampler  != sampler
            || draw->blend    != blend
            || draw->position != position)
        {
            draw = &draws.emplace_back(Draw{
                .vertex_offset = u32(vertices.size()),
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

    auto draw_mesh = [&](SceneMesh* mesh) {
        auto pos = scene_tree_get_position(mesh->parent);

        for (auto& segment : mesh->segments) {
            draws.emplace_back(Draw {
                .vertex_offset = u32(vertices.size()) + segment.vertex_offset,
                .first_index = u32(indices.size()) + segment.first_index,
                .index_count = u32(segment.index_count),
                .clip = aabb_inner(default_clip, {
                    pos + segment.clip.min,
                    pos + segment.clip.max,
                    minmax
                }),
                .image = segment.image.get() ?: render.white.get(),
                .sampler = segment.sampler.get() ?: render.sampler.get(),
                .blend = segment.blend,
                .position = pos + mesh->offset,
            });
        }

        vertices.insert_range(vertices.end(), mesh->vertices);
        indices.insert_range(indices.end(), mesh->indices);
    };

    auto draw_texture = [&](SceneTexture* texture) {
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

        auto base_vtx = vertices.size() - draw->vertex_offset;
        draw->index_count += 6;
        for (auto idx : {0, 2, 1, 1, 2, 3}) {
            indices.emplace_back(base_vtx + idx);
        }

        auto translation = scene_tree_get_position(texture->parent);
        auto push_vtx = [&](vec2f32 pos, vec2f32 uv = {}) {
            vertices.emplace_back(SceneVertex {
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

    scene_iterate<SceneIterateDirection::back_to_front>(
        scene->root.get(),
        scene_iterate_default,
        [&](SceneNode* node) {
            if (auto* texture = dynamic_cast<SceneTexture*>(node)) {
                draw_texture(texture);
            } else if (auto* mesh = dynamic_cast<SceneMesh*>(node)) {
                draw_mesh(mesh);
            }
        },
        scene_iterate_default);

    auto gpu = scene->gpu;

    auto make_gpu = [&]<typename T>(std::span<T> data) {
        GpuArray<T> arr{gpu_buffer_create(gpu, data.size_bytes(), {}), 0};
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
    }, [&](GpuRenderpass& pass) {
        pass.set_viewports({{{}, target->extent(), xywh}});

        rect2f32 scissor = default_clip;
        scissor.origin -= viewport.origin;
        pass.set_scissors({scissor});

        pass.bind_shaders({scene->render.vertex.get(), scene->render.fragment.get()});
        pass.bind_index_buffer(gpu_indices.buffer.get(), 0, VK_INDEX_TYPE_UINT32);

        for (auto& draw : draws) {
            pass.set_blend_state({draw.blend});

            rect2f32 clip = draw.clip;
            clip.extent /= 2.f;
            clip.origin += clip.extent - viewport.origin;

            auto draw_scale = 2.f / viewport.extent;
            pass.push_constants(0, view_bytes(SceneRenderInput {
                .vertices = gpu_vertices.device(),
                .scale = draw_scale,
                .offset = (draw.position - viewport.origin) * draw_scale - 1.f,
                .texture = {draw.image, render.sampler.get()},
                .clip = clip,
            }));

            pass.draw_indexed({
                .index_count = draw.index_count,
                .instance_count = 1,
                .first_index = draw.first_index,
                .vertex_offset = draw.vertex_offset,
                .first_instance = 0
            });
        }
    });
}
