#include "internal.hpp"

#include <core/math.hpp>
#include <core/color.hpp>
#include <core/log.hpp>

#include "scene_render_vert.hpp"
#include "scene_render_frag.hpp"

auto get_pipeline(SceneRenderer* renderer, GpuFormat format) -> GpuPipeline*
{
    auto iter = renderer->pipelines.find(format);
    if (iter != renderer->pipelines.end()) return iter->second.get();

    auto pipeline = gpu_pipeline_create(renderer->gpu, {
        .format = format,
        .shaders = {{
            {
                .stage = VK_SHADER_STAGE_VERTEX_BIT,
                .code  = scene_render_vert,
                .entry = "main",
            },
            {
                .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                .code  = scene_render_frag,
                .entry = "main",
            }
        }},
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .blend_mode = GpuBlendMode::premultiplied,
    });

    renderer->pipelines.emplace(format, pipeline);

    return pipeline.get();
}

auto scene_renderer_create(Gpu* gpu) -> Ref<SceneRenderer>
{
    auto renderer = ref_create<SceneRenderer>();

    renderer->gpu = gpu;

    renderer->white = gpu_image_create(renderer->gpu, {
        .extent = {1, 1},
        .format = gpu_format_from_drm(DRM_FORMAT_ABGR8888),
        .usage = GpuImageUsage::texture | GpuImageUsage::transfer_dst
    });
    gpu_copy_memory_to_image(renderer->white.get(), view_bytes(color_from_hex("#FFFFFF")), {{{{1, 1}}}});

    renderer->indices = gpu_buffer_create(renderer->gpu, sizeof(u32) * 6, {});
    auto indices = std::to_array<u32>({ 0, 2, 1, 1, 2, 3 });
    std::memcpy(renderer->indices->host_address, indices.data(), indices.size() * sizeof(u32));

    renderer->nearest = gpu_sampler_create(renderer->gpu, {
        .mag = VK_FILTER_NEAREST,
        .min = VK_FILTER_NEAREST,
    });

    return renderer;
}

void scene_render(SceneRenderer* renderer, const SceneRenderInfo& info)
{
    std::vector<SceneQuad> quads;

    ankerl::unordered_dense::set<void*> reads;
    ankerl::unordered_dense::set<void*> writes{info.target};

    auto draw_texture = [&](SceneTexture* texture, vec2f32 translation, f32 opacity) {
        aabb2f32 src = texture->src;
        rect2f32 dst = texture->dst;

        dst.origin += translation;

        if (!aabb_intersects<f32>(info.viewport, dst)) {
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
    }(info.root, {}, 1.f);

    if (info.damage && info.options.contains(SceneRenderOption::show_damage)) {
        auto step = std::max(0.f, 1.f / info.damage->sections.size());
        usz i = 0;
        for (auto& band : info.damage->bands) {
            for (auto& section : std::span(info.damage->sections).subspan(band.start, band.count)) {
                auto hsv = vec4f32{step * i++, 1.f, 1.f, 0.5f};
                auto rgb = color_hsv_to_rgb(hsv);
                quads.emplace_back(SceneQuad {
                    .dst = aabb_inner<f32>({{section.min, band.min}, {section.max, band.max}, minmax}, info.viewport),
                    .texture = {renderer->white.get(), renderer->nearest.get()},
                    .src = {{}, {1, 1}, xywh},
                    .tint = vec_cast<u8>(rgb * 255.f),
                });
            }
        }
    }

    auto* gpu = renderer->gpu;

    auto gpu_rects = gpu_buffer_create(gpu, quads.size() * sizeof(SceneQuad), {});
    std::memcpy(gpu_rects->host_address, quads.data(), gpu_rects->size);
    reads.emplace(gpu_rects.get());

    // Record

    auto draw_scale = 2.f / info.viewport.extent;
    auto draw_offset = -info.viewport.origin * draw_scale - 1.f;

    auto cmd = gpu_record(gpu);
    gpu_barrier(cmd, reads, writes);

    gpu_begin_rendering(cmd, {
        .target = info.target,
        .clear_color = {{0,0,0,0}},
    });

    gpu_set_viewports(cmd, {{{{}, vec_cast<f32>(info.target->base()->extent), xywh}}});
    gpu_set_scissors( cmd, {{{{}, vec_cast<i32>(info.target->base()->extent), xywh}}});

    gpu_bind_pipeline(cmd, get_pipeline(renderer, info.target->base()->format));
    gpu_bind_index_buffer(cmd, renderer->indices.get(), 0, VK_INDEX_TYPE_UINT32);

    gpu_push_constants(cmd, 0, view_bytes(SceneRenderInput {
        .quads = gpu_rects->device<SceneQuad>(),
        .offset = draw_offset,
        .scale = draw_scale,
    }));

    gpu_draw_indexed(cmd, {
        .index_count = 6,
        .instance_count = u32(quads.size()),
    });

    gpu_end_rendering(cmd);
}
