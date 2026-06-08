#include "internal.hpp"

#include <core/math.hpp>
#include <core/color.hpp>

#include "scene_render_vert.hpp"
#include "scene_render_frag.hpp"

SceneRenderer::~SceneRenderer()
{
}

auto scene_renderer_create(Gpu* gpu) -> Ref<SceneRenderer>
{
    auto renderer = ref_create<SceneRenderer>();

    renderer->gpu = gpu;

    renderer->vertex   = gpu_shader_create(renderer->gpu, {
        .stage = VK_SHADER_STAGE_VERTEX_BIT,
        .code  = scene_render_vert,
        .entry = "main",
    });
    renderer->fragment = gpu_shader_create(renderer->gpu, {
        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .code  = scene_render_frag,
        .entry = "main",
    });

    renderer->white = gpu_image_create(renderer->gpu, {
        .extent = {1, 1},
        .format = gpu_format_from_drm(DRM_FORMAT_ABGR8888),
        .usage = GpuImageUsage::texture | GpuImageUsage::transfer_dst
    });

    gpu_copy_memory_to_image(renderer->white.get(), as_bytes(ptr_to(color_from_hex("#FFFFFF")), 4), {{{{1, 1}}}});

    renderer->nearest = gpu_sampler_create(renderer->gpu, {
        .mag = VK_FILTER_NEAREST,
        .min = VK_FILTER_NEAREST,
    });

    return renderer;
}

void scene_render(SceneRenderer* renderer, SceneNode* node, GpuImage* target, rect2f32 viewport)
{
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
        f32 opacity;
    };

    std::vector<SceneVertex> vertices;
    std::vector<u32> indices;
    std::vector<Draw> draws;

    aabb2f32 default_clip = viewport;

    auto get_opacity = [](SceneNode* node) {
        f32 opacity = 1.f;
        while (node->parent) {
            opacity *= node->parent->opacity;
            node = node->parent;
        }
        return opacity;
    };

    auto get_draw = [&draws, &vertices, &indices](
        aabb2f32 clip, GpuImage* image, GpuSampler* sampler, GpuBlendMode blend, vec2f32 position, f32 opacity)
    {
        auto* draw = draws.empty() ? nullptr : &draws.back();
        if (  !draw
            || draw->clip     != clip
            || draw->image    != image
            || draw->sampler  != sampler
            || draw->blend    != blend
            || draw->position != position
            || draw->opacity  != opacity)
        {
            draw = &draws.emplace_back(Draw{
                .vertex_offset = u32(vertices.size()),
                .first_index = u32(indices.size()),
                .clip = clip,
                .image = image,
                .sampler = sampler,
                .blend = blend,
                .position = position,
                .opacity = opacity,
            });
        }
        return draw;
    };

    auto draw_texture = [&](SceneTexture* texture) {
        aabb2f32 src = texture->src;
        aabb2f32 dst = texture->dst;

        auto translation = scene_tree_get_position(texture->parent);

        {
            rect2f32 bounds = dst;
            bounds.origin += translation;
            if (!rect_intersects(viewport, bounds)) {
                return;
            }
        }

        //  0 ---- 1
        //  | a /  |  a = 0,2,1
        //  |  / b |  b = 1,2,3
        //  2 ---- 3

        auto* draw = get_draw(default_clip,
            texture->image.get()   ?: renderer->white.get(),
            texture->sampler.get() ?: renderer->nearest.get(),
            texture->blend,
            {},
            get_opacity(texture));

        auto base_vtx = vertices.size() - draw->vertex_offset;
        draw->index_count += 6;
        for (auto idx : {0, 2, 1, 1, 2, 3}) {
            indices.emplace_back(base_vtx + idx);
        }

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
        node,
        scene_iterate_default,
        OverloadSet {
            draw_texture,
            [](SceneInputRegion*) {},
        },
        scene_iterate_default);

    auto gpu = renderer->gpu;

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

    gpu_protect(gpu, renderer->white.get());
    for (auto& draw : draws) {
        gpu_protect(gpu, draw.image);
    }

    // Record

    gpu_render(gpu, {
        .target = target,
        .clear_color = {{0,0,0,1}},
    }, [&](GpuRenderPass* pass) {
        gpu_set_viewports(pass, {{{{}, vec_cast<f32>(target->extent()), xywh}}});

        rect2f32 scissor = default_clip;
        scissor.origin -= viewport.origin;
        gpu_set_scissors(pass, {{rect_cast<i32>(scissor)}});

        gpu_set_blend_state(pass, {{GpuBlendMode::premultiplied}});

        gpu_bind_shaders(pass, {{renderer->vertex.get(), renderer->fragment.get()}});
        gpu_bind_index_buffer(pass, gpu_indices.buffer.get(), 0, VK_INDEX_TYPE_UINT32);

        for (auto& draw : draws) {

            rect2f32 clip = draw.clip;
            clip.extent /= 2.f;
            clip.origin += clip.extent - viewport.origin;

            auto draw_scale = 2.f / viewport.extent;

            u32 flags = 0;
            if (draw.blend == GpuBlendMode::premultiplied) {
                flags |= SCENE_DRAW_FLAG_PREMULTIPLIED;
            }

            gpu_push_constants(pass, 0, view_bytes(SceneRenderInput {
                .vertices = gpu_vertices.device(),
                .scale = draw_scale,
                .offset = (draw.position - viewport.origin) * draw_scale - 1.f,
                .texture = {draw.image, draw.sampler},
                .clip = clip,
                .opacity = draw.opacity,
                .flags = flags,
            }));

            gpu_draw_indexed(pass, {
                .index_count = draw.index_count,
                .instance_count = 1,
                .first_index = draw.first_index,
                .vertex_offset = draw.vertex_offset,
                .first_instance = 0
            });
        }
    });
}
