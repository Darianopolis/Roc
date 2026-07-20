#include "internal.hpp"

#include <core/math.hpp>
#include <core/color.hpp>
#include <core/log.hpp>
#include <core/chrono.hpp>

#include <gpu/internal.hpp>

#include "shader/render.h"

#include "scene_shader_bin.hpp"
#include "scene_shader_pixel.hpp"

auto scene_renderer_create(Gpu* gpu) -> Ref<SceneRenderer>
{
    auto renderer = ref_create<SceneRenderer>();

    renderer->gpu = gpu;

    renderer->white = gpu_image_create(renderer->gpu, {
        .extent = {1, 1},
        .format = gpu_format_from_drm(DRM_FORMAT_ABGR8888),
        .usage = GpuImageUsage::sampled | GpuImageUsage::transfer_dst
    });
    gpu_copy_memory_to_image(renderer->white.get(), view_bytes(color_from_hex("#FFFFFF")), {{{{1, 1}}}});

    renderer->nearest = gpu_sampler_create(renderer->gpu, {
        .mag = VK_FILTER_NEAREST,
        .min = VK_FILTER_NEAREST,
    });

    renderer->pool = gpu_image_pool_create(renderer->gpu);

    renderer->compute_bin = gpu_pipeline_create_compute(gpu, {
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .code = scene_shader_bin,
        .entry = "main",
    });

    renderer->compute_pixel = gpu_pipeline_create_compute(gpu, {
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .code = scene_shader_pixel,
        .entry = "main",
    });

    return renderer;
}

#define SCENE_NOISY_RENDER 0

void scene_render(SceneRenderer* renderer, const SceneRenderInfo& info)
{
    [[maybe_unused]] auto start = std::chrono::steady_clock::now();

    debug_assert(info.target->base()->usage.contains(GpuImageUsage::storage));

    auto extent = info.target->base()->extent;

    auto pixel_size = vec_cast<f32>(extent) / info.viewport.extent;
    auto layout_to_pixel = [&](vec2f32 layout) {
        return (layout - info.viewport.origin) * pixel_size;
    };

    // 1. Prepare

    std::vector<SceneRenderQuad> quads;
    std::vector<u8> quad_opaque_flags;
    std::vector<aabb2f32> quad_bounds;

    quads.emplace_back();
    quad_opaque_flags.emplace_back();
    quad_bounds.emplace_back();

    std::flat_set<GpuResource*> reads;

    if (info.damage && info.options.contains(SceneRenderOption::show_damage)) {
        auto step = std::max(0.f, 1.f / num_cast<f32>(info.damage->sections.size()));
        f32 hue = 0;
        for (auto& band : info.damage->bands) {
            for (auto& section : std::span(info.damage->sections).subspan(band.start, band.count)) {
                auto hsv = vec4f32{hue, 1.f, 1.f, 0.25f};
                hue += step;
                auto rgb = color_hsv_to_rgb(hsv);
                auto dst = aabb_inner<f32>({{section.min, band.min}, {section.max, band.max}, minmax}, info.viewport);
                dst.min = layout_to_pixel(dst.min);
                dst.max = layout_to_pixel(dst.max);
                quads.emplace_back(SceneRenderQuad {
                    .dst = dst,
                    .texture = {renderer->white.get(), renderer->nearest.get()},
                    .src = {{}, {1, 1}, xywh},
                    .tint = pack_unorm<u8>(rgb),
                });
                quad_bounds.emplace_back(dst);
                quad_opaque_flags.emplace_back(0);
            }
        }
    }

    auto collect_texture = [&](SceneTexture* texture, vec2f32 translation, f32 opacity) {
        if (quads.size() >= std::numeric_limits<SCENE_RENDER_QUAD_INDEX_TYPE>::max()) return;

        rect2f32 dst = texture->dst;
        dst.origin += translation;

        if (!aabb_intersects<f32>(info.viewport, dst)) {
            return;
        }

        vec4f32 tint = texture->tint;
        tint *= opacity;
        if (tint.w == 0.f) return;

        aabb2f32 pixel_dst = dst;
        pixel_dst.min = layout_to_pixel(pixel_dst.min);
        pixel_dst.max = layout_to_pixel(pixel_dst.max);

        auto image   = texture->image.get()   ?: renderer->white.get();
        auto sampler = texture->sampler.get() ?: renderer->nearest.get();

        u32 flags = 0;

        if (texture->flags.contains(SceneTextureFlag::premultiplied)) {
            flags |= SCENE_RENDER_FLAG_PREMULTIPLIED;
        }

        if (!texture->image && tint.w == 1.f) {
            flags |= SCENE_RENDER_FLAG_OPAQUE;
        }

        quads.emplace_back(SceneRenderQuad {
            .dst = pixel_dst,
            .texture = {image, sampler},
            .src = texture->src,
            .tint = pack_unorm<u8>(srgb_oetf(tint)),
            .flags = flags,
        });
        quad_bounds.emplace_back(pixel_dst);
        quad_opaque_flags.emplace_back((flags & SCENE_RENDER_FLAG_OPAQUE) != 0);

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

    usz quad_count = quad_bounds.size();

    // 2. Coarse binning CPU pass

    [[maybe_unused]] auto coarse_bin_start = std::chrono::steady_clock::now();

    auto coarse_bin_counts = (extent + literal_cast<u32>(SCENE_RENDER_COARSE_BIN_SIZE) - 1u) / literal_cast<u32>(SCENE_RENDER_COARSE_BIN_SIZE);
    auto coarse_bin_count = coarse_bin_counts.x * coarse_bin_counts.y + SCENE_RENDER_RESERVED_COARSE_BIN_COUNT;

    std::vector<SCENE_RENDER_QUAD_INDEX_TYPE> coarse_bin_next_slot;
    std::vector<u32> coarse_bin_remaps;
    std::vector<SceneRenderBin> coarse_bins;
    coarse_bin_next_slot.resize(coarse_bin_count);
    coarse_bins.resize(coarse_bin_count);
    coarse_bin_remaps.resize(coarse_bin_count);
    for (u32 i = 0; i < coarse_bin_count; ++i) {
        coarse_bin_remaps[i] = i;
    }

    auto allocate_coarse_bin = [&] {
        u32 index = num_cast<u32>(coarse_bins.size());
        coarse_bins.emplace_back();
        return index;
    };

    for (u32 i = 0; i < quad_count; ++i) {
        vec2i32 bin_start = vec_cast<i32>(vec_floor(quad_bounds[i].min / literal_cast<f32>(SCENE_RENDER_COARSE_BIN_SIZE)));
        vec2i32 bin_end   = vec_cast<i32>(vec_ceil( quad_bounds[i].max / literal_cast<f32>(SCENE_RENDER_COARSE_BIN_SIZE)));
        bin_start = vec_max(bin_start, vec2i32{});
        bin_end   = vec_min(bin_end,   vec_cast<i32>(coarse_bin_counts));

        for (i32 bin_y = bin_start.y; bin_y < bin_end.y; ++bin_y) {
            u32 bin_row_index = num_cast<u32>(bin_y) * coarse_bin_counts.x + SCENE_RENDER_RESERVED_COARSE_BIN_COUNT;
            for (i32 bin_x = bin_start.x; bin_x < bin_end.x; ++bin_x) {
                u32 original_bin = bin_row_index + num_cast<u32>(bin_x);

                usz bin = coarse_bin_remaps[original_bin];
                if (coarse_bin_next_slot[original_bin] >= SCENE_RENDER_QUADS_PER_BIN) [[unlikely]] {
                    u32 new_bin = allocate_coarse_bin();       // Allocate a new bin
                    coarse_bin_remaps[original_bin] = new_bin; // Point the head of the stack to the new bin
                    coarse_bin_next_slot[original_bin] = 0;    // Reset the bin slot
                    coarse_bins[bin].next_bin = new_bin;       // Update the next bin link
                    bin = new_bin;                             // Update the bin index
                }
                coarse_bins[bin].quads[coarse_bin_next_slot[original_bin]++] = num_cast<SCENE_RENDER_QUAD_INDEX_TYPE>(i);
            }
        }
    }

    std::vector<SceneRenderCoarseBinInfo> coarse_bin_infos;
    coarse_bin_infos.resize(coarse_bin_count);

    [[maybe_unused]] auto coarse_bin_complete = std::chrono::steady_clock::now();

    u32 fine_bin_slots = 0;
    for (u32 bin = 1; bin < coarse_bin_count; ++bin) {
        u32 depth = coarse_bin_next_slot[bin];
        u32 next = bin;
        while ((next = coarse_bins[next].next_bin)) {
            depth += SCENE_RENDER_QUADS_PER_BIN;
        }

        coarse_bin_infos[bin] = {
            .offset = fine_bin_slots,
            .depth = depth,
        };

        fine_bin_slots += SCENE_RENDER_COARSE_FINE_BIN_RATIO * SCENE_RENDER_COARSE_FINE_BIN_RATIO * depth;
    }

    // 3. Fine binning GPU pass

    auto* gpu = renderer->gpu;

    auto cmd = gpu_record(gpu);

    auto copy_to_gpu = [&]<typename T>(std::span<const T> elements) {
        auto buffer = gpu_buffer_create(gpu, elements.size() * sizeof(T), {});
        std::memcpy(buffer->host_address, elements.data(), buffer->size);
        gpu_protect(cmd, buffer);
        return buffer;
    };

    auto gpu_quad_bounds = copy_to_gpu.operator()<aabb2f32>(quad_bounds);
    auto gpu_quad_opaque_flags = copy_to_gpu.operator()<u8>(quad_opaque_flags);

    auto gpu_quads = copy_to_gpu.operator()<SceneRenderQuad>(quads);

    auto gpu_coarse_bins = copy_to_gpu.operator()<SceneRenderBin>(coarse_bins);
    auto gpu_coarse_bin_infos = copy_to_gpu.operator()<SceneRenderCoarseBinInfo>(coarse_bin_infos);

    auto gpu_fine_bins = gpu_buffer_create(gpu, fine_bin_slots * sizeof(SCENE_RENDER_QUAD_INDEX_TYPE), {});
    gpu_protect(cmd, gpu_fine_bins);

    gpu_barrier(cmd, reads, {{gpu_fine_bins.get()}});

    vec2u32 fine_bin_counts = coarse_bin_counts * literal_cast<u32>(SCENE_RENDER_COARSE_FINE_BIN_RATIO);

    gpu_bind_pipeline(cmd, renderer->compute_bin.get());
    gpu_push_constants(cmd, 0, view_bytes(SceneRenderBinPassInput {
        .quad_bounds = gpu_quad_bounds->device<aabb2f32>(),
        .quad_opaque_flags = gpu_quad_opaque_flags->device<u8>(),
        .coarse_bins = gpu_coarse_bins->device<SceneRenderBin>(),
        .coarse_bin_infos = gpu_coarse_bin_infos->device<SceneRenderCoarseBinInfo>(),
        .fine_bins = gpu_fine_bins->device<SCENE_RENDER_QUAD_INDEX_TYPE>(),
        .coarse_bin_row_stride = coarse_bin_counts.x,
        .extent = fine_bin_counts,
    }));

    gpu_dispatch(cmd, vec_join((fine_bin_counts + literal_cast<u32>(SCENE_RENDER_BIN_PASS_LOCAL_SIZE - 1))
                                                / literal_cast<u32>(SCENE_RENDER_BIN_PASS_LOCAL_SIZE), 1u));

    // 3. Pixel GPU pass

    gpu_barrier(cmd, {{gpu_fine_bins.get()}}, {{info.target}});

    gpu_bind_pipeline(cmd, renderer->compute_pixel.get());
    gpu_push_constants(cmd, 0, view_bytes(SceneRenderPixelPassInput {
        .quad_bounds = gpu_quad_bounds->device<aabb2f32>(),
        .quads = gpu_quads->device<SceneRenderQuad>(),
        .coarse_bin_infos = gpu_coarse_bin_infos->device<SceneRenderCoarseBinInfo>(),
        .fine_bins = gpu_fine_bins->device<SCENE_RENDER_QUAD_INDEX_TYPE>(),
        .coarse_bin_row_stride = coarse_bin_counts.x,
        .target = info.target,
        .extent = extent,
    }));

    gpu_dispatch(cmd, vec_join((extent + literal_cast<u32>(SCENE_RENDER_PIXEL_PASS_LOCAL_SIZE - 1))
                                       / literal_cast<u32>(SCENE_RENDER_PIXEL_PASS_LOCAL_SIZE), 1u));

    [[maybe_unused]] auto end = std::chrono::steady_clock::now();

#if SCENE_NOISY_RENDER
    log_debug("Render dispatched {} quads in {} (coarse: {})", quad_count, FmtDuration{end - start}, FmtDuration{coarse_bin_complete - coarse_bin_start});
    log_trace("  coarse bins: {:6} | {:3} * {:3} + {:4} = {}",
        coarse_bin_counts.x * coarse_bin_counts.y,
        coarse_bin_counts.x, coarse_bin_counts.y,
        coarse_bins.size() - (coarse_bin_counts.x * coarse_bin_counts.y) - 1,
        FmtBytes(coarse_bins.size() * (sizeof(SceneRenderBin) + sizeof(SceneRenderCoarseBinInfo))));
    log_trace("    fine bins: {:6} | {:3} * {:3} * {:4.1f} = {}",
        fine_bin_counts.x * fine_bin_counts.y,
        fine_bin_counts.x, fine_bin_counts.y,
        f32(fine_bin_slots) / f32(fine_bin_counts.x * fine_bin_counts.y),
        FmtBytes(fine_bin_slots * sizeof(SCENE_RENDER_QUAD_INDEX_TYPE)));
#endif
}
