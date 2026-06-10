#include "internal.hpp"

#include <core/math.hpp>
#include <core/color.hpp>
#include <core/log.hpp>

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
    gpu_copy_memory_to_image(renderer->white.get(), view_bytes(color_from_hex("#FFFFFF")), {{{{1, 1}}}});

    renderer->indices = {gpu_buffer_create(renderer->gpu, sizeof(u32) * 6, {}), 0};
    auto indices = std::to_array<u32>({ 0, 2, 1, 1, 2, 3 });
    std::memcpy(renderer->indices.host(), indices.data(), indices.size() * sizeof(u32));

    renderer->nearest = gpu_sampler_create(renderer->gpu, {
        .mag = VK_FILTER_NEAREST,
        .min = VK_FILTER_NEAREST,
    });

    return renderer;
}

void scene_render(SceneRenderer* renderer, SceneNode* node, GpuImage* target, rect2f32 viewport)
{
    std::vector<SceneQuad> quads;

    ankerl::unordered_dense::set<void*> reads;
    ankerl::unordered_dense::set<void*> writes{target};

    auto draw_texture = [&](SceneTexture* texture, vec2f32 translation, f32 opacity) {
        aabb2f32 src = texture->src;
        rect2f32 dst = texture->dst;

        dst.origin += translation;

        if (!rect_intersects(viewport, dst)) {
            return;
        }

        auto image   = texture->image.get()   ?: renderer->white.get();
        auto sampler = texture->sampler.get() ?: renderer->nearest.get();

        u32 flags = 0;
        if (texture->blend == GpuBlendMode::premultiplied) {
            flags |= SCENE_DRAW_FLAG_PREMULTIPLIED;
        }

        quads.emplace_back(SceneQuad {
            .dst = dst,
            .texture = {image, sampler},
            .src = src,
            .tint = vec_cast<u8>((vec_cast<f32>(texture->tint) / 255.f) * (opacity * 255.f)),
            .flags = flags,
        });

        reads.emplace(image);
        reads.emplace(sampler);
    };

    [&](this auto&& visit, SceneNode* node, vec2f32 translation, f32 opacity) -> void {
        scene_visit(node, OverloadSet {
            [&](SceneTexture* texture) {
                draw_texture(texture, translation, opacity);
            },
            [&](SceneTree* tree) {
                if (!tree->enabled) return;
                translation += tree->translation;
                opacity *= tree->opacity;
                for (auto* child : tree->children) {
                    visit(child, translation, opacity);
                }
            },
            [&](SceneInputRegion*) {}
        });
    }(node, {}, 1.f);

    GpuArray<SceneQuad> gpu_rects{gpu_buffer_create(renderer->gpu, quads.size() * sizeof(SceneQuad), {}), 0};
    std::memcpy(gpu_rects.host(), quads.data(), quads.size() * sizeof(SceneQuad));
    reads.emplace(gpu_rects.buffer.get());

    // Record

    auto draw_scale = 2.f / viewport.extent;
    auto draw_offset = -viewport.origin * draw_scale - 1.f;

    gpu_render(renderer->gpu, {
        .target = target,
        .clear_color = {{0,0,0,1}},
        .reads = &reads,
        .writes = &writes,
    }, [&](GpuRenderPass* pass) {
        gpu_set_viewports(pass, {{{{}, vec_cast<f32>(target->extent()), xywh}}});
        gpu_set_scissors( pass, {{{{}, vec_cast<i32>(target->extent()), xywh}}});

        gpu_set_blend_state(pass, {{GpuBlendMode::premultiplied}});

        gpu_bind_shaders(pass, {{renderer->vertex.get(), renderer->fragment.get()}});
        gpu_bind_index_buffer(pass, renderer->indices.buffer.get(), 0, VK_INDEX_TYPE_UINT32);

        gpu_push_constants(pass, 0, view_bytes(SceneRenderInput {
            .quads = gpu_rects.device(),
            .offset = draw_offset,
            .scale = draw_scale,
        }));

        gpu_draw_indexed(pass, {
            .index_count = 6,
            .instance_count = u32(quads.size()),
        });
    });
}
