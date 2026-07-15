#include "internal.hpp"

#include <core/math.hpp>
#include <core/color.hpp>
#include <core/log.hpp>

#include <gpu/internal.hpp>

#include "shader/render.h"

#include "scene_render_vert.hpp"
#include "scene_render_frag.hpp"
#include "scene_render_comp.hpp"
#include "scene_bin_comp.hpp"

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
        .blend_direction = GpuBlendDirection::front_to_back,
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

    renderer->compute = gpu_pipeline_create_compute(gpu, {
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .code = scene_render_comp,
        .entry = "main",
    });

    renderer->bin = gpu_pipeline_create_compute(gpu, {
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .code = scene_bin_comp,
        .entry = "main",
    });

    return renderer;
}

void scene_render(SceneRenderer* renderer, const SceneRenderInfo& info)
{
    bool use_compute = info.options.contains(SceneRenderOption::use_compute);

    auto extent = info.target->base()->extent;

    auto pixel_size = vec_cast<f32>(extent) / info.viewport.extent;
    auto layout_to_pixel = [&](vec2f32 layout) {
        return (layout - info.viewport.origin) * pixel_size;
    };

    // 1. Prepare

    std::vector<SceneQuad> quads;
    std::vector<u8> quad_opaque_flags;
    std::vector<aabb2f32> quad_bounds;

    quads.emplace_back();
    quad_opaque_flags.emplace_back();
    quad_bounds.emplace_back();

    std::flat_set<GpuResource*> reads;

    auto collect_texture = [&](SceneTexture* texture, vec2f32 translation, f32 opacity) {
        if (use_compute && quads.size() >= std::numeric_limits<SCENE_QUAD_INDEX_TYPE>::max()) return;

        rect2f32 dst = texture->dst;
        dst.origin += translation;

        if (!aabb_intersects<f32>(info.viewport, dst)) {
            return;
        }

        aabb2f32 pixel_dst = dst;
        pixel_dst.min = layout_to_pixel(pixel_dst.min);
        pixel_dst.max = layout_to_pixel(pixel_dst.max);

        auto image   = texture->image.get()   ?: renderer->white.get();
        auto sampler = texture->sampler.get() ?: renderer->nearest.get();

        u32 flags = 0;
        if (texture->flags.contains(SceneTextureFlag::premultiplied)) {
            flags |= SCENE_DRAW_FLAG_PREMULTIPLIED;
        }

        vec4f32 tint = texture->tint;
        tint *= opacity;
        if (tint.w == 0.f) return;

        quads.emplace_back(SceneQuad {
            .dst = pixel_dst,
            .texture = {image, sampler},
            .src = texture->src,
            .tint = pack_unorm<u8>(srgb_oetf(tint)),
            .flags = flags,
        });
        quad_bounds.emplace_back(pixel_dst);
        quad_opaque_flags.emplace_back(!texture->image && tint.w == 1.f);

        reads.insert(image);
        reads.insert(sampler);
    };

    [&](this auto&& visit, SceneNode* node, vec2f32 translation, f32 opacity) -> void {
        scene_visit(node, OverloadSet {
            [&](SceneTexture* texture) {
                collect_texture(texture, translation, opacity);
            },
            [&](SceneTree* tree) {
                if (!tree->enabled) return;
                translation += tree->translation;
                opacity *= tree->opacity;
                for (auto* child : tree->children | std::views::reverse) {
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
                auto dst = aabb_inner<f32>({{section.min, band.min}, {section.max, band.max}, minmax}, info.viewport);
                quads.emplace_back(SceneQuad {
                    .dst = dst,
                    .texture = {renderer->white.get(), renderer->nearest.get()},
                    .src = {{}, {1, 1}, xywh},
                    .tint = vec_cast<u8>(rgb * 255.f),
                });
                dst.min = layout_to_pixel(dst.min);
                dst.max = layout_to_pixel(dst.max);
                quad_bounds.emplace_back(dst);
                quad_opaque_flags.emplace_back(0);
            }
        }
    }

    auto* gpu = renderer->gpu;

    auto gpu_quads = gpu_buffer_create(gpu, quads.size() * sizeof(SceneQuad), {});
    std::memcpy(gpu_quads->host_address, quads.data(), gpu_quads->size);

    if (use_compute) {
        auto gpu_quad_bounds = gpu_buffer_create(gpu, quads.size() * sizeof(aabb2f32), {});
        std::memcpy(gpu_quad_bounds->host_address, quad_bounds.data(), gpu_quad_bounds->size);

        auto gpu_quad_opaque_flags = gpu_buffer_create(gpu, quads.size() * sizeof(u8), {});
        std::memcpy(gpu_quad_opaque_flags->host_address, quad_opaque_flags.data(), gpu_quad_opaque_flags->size);

        auto bins_horizontal = (extent.x + SCENE_BIN_SIZE - 1) / SCENE_BIN_SIZE;
        auto bins_vertical   = (extent.y + SCENE_BIN_SIZE - 1) / SCENE_BIN_SIZE;
        auto bin_count = bins_horizontal * bins_vertical + 1;
        auto extra_bins_start = bin_count;
        bin_count += (bins_horizontal * bins_vertical) * 3;
        auto gpu_bins = gpu_buffer_create(gpu, bin_count * sizeof(SceneRenderBin), {});
        gpu_bins->host<SceneRenderBin>()->next_bin = extra_bins_start;

        auto cmd = gpu_record(gpu);
        gpu_protect(cmd, gpu_quads);
        gpu_protect(cmd, gpu_quad_bounds);
        gpu_protect(cmd, gpu_bins);
        gpu_protect(cmd, gpu_quad_opaque_flags);

        // 2. Bin

        gpu_barrier(cmd, reads, {{gpu_bins.get()}});

        gpu_bind_pipeline(cmd, renderer->bin.get());
        gpu_push_constants(cmd, 0, view_bytes(SceneRenderBinInput {
            .quad_bounds = gpu_quad_bounds->device<aabb2f32>(),
            .quad_opaque_flags = gpu_quad_opaque_flags->device<u8>(),
            .quad_count = u32(quads.size()),
            .bins = gpu_bins->device<SceneRenderBin>(),
            .bin_count = bin_count,
            .row_stride = bins_horizontal,
            .extent = {bins_horizontal, bins_vertical},
        }));

        gpu_dispatch(cmd, {
            (bins_horizontal + SCENE_BIN_DISPATCH_SIZE - 1) / SCENE_BIN_DISPATCH_SIZE,
            (bins_vertical   + SCENE_BIN_DISPATCH_SIZE - 1) / SCENE_BIN_DISPATCH_SIZE,
            1u
        });

        // 3. Draw

        gpu_barrier(cmd, {{gpu_bins.get()}}, {{info.target}});

        gpu_bind_pipeline(cmd, renderer->compute.get());
        gpu_push_constants(cmd, 0, view_bytes(SceneRenderComputeInput {
            .quad_bounds = gpu_quad_bounds->device<aabb2f32>(),
            .quads = gpu_quads->device<SceneQuad>(),
            .quad_count = u32(quads.size()),
            .bins = gpu_bins->device<SceneRenderBin>(),
            .row_stride = bins_horizontal,
            .target = info.target,
            .extent = extent,
        }));

        gpu_dispatch(cmd, {
            (extent.x + SCENE_COMPUTE_DISPATCH_SIZE - 1) / SCENE_COMPUTE_DISPATCH_SIZE,
            (extent.y + SCENE_COMPUTE_DISPATCH_SIZE - 1) / SCENE_COMPUTE_DISPATCH_SIZE,
            1u
        });
    } else {
        auto cmd = gpu_record(gpu);
        gpu_protect(cmd, gpu_quads);

        gpu_barrier(cmd, reads, {{info.target}});

        gpu_begin_rendering(cmd, {
            .target = info.target,
            .clear_color = {{0,0,0,0}},
        });

        gpu_set_viewports(cmd, {{{{}, vec_cast<f32>(info.target->base()->extent), xywh}}});
        gpu_set_scissors( cmd, {{{{}, vec_cast<i32>(info.target->base()->extent), xywh}}});

        gpu_bind_pipeline(cmd, get_pipeline(renderer, info.target->base()->format));
        gpu_bind_index_buffer(cmd, renderer->indices.get(), 0, VK_INDEX_TYPE_UINT32);

        gpu_push_constants(cmd, 0, view_bytes(SceneRenderInput {
            .quads = gpu_quads->device<SceneQuad>() + 1,
            .offset = -vec2f32(1.f, 1.f),
            .scale = 2.f / vec_cast<f32>(extent),
        }));

        gpu_draw_indexed(cmd, {
            .index_count = 6,
            .instance_count = u32(quads.size() - 1),
        });

        gpu_end_rendering(cmd);
    }
}
