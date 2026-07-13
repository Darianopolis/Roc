#include "internal.hpp"

#include <core/log.hpp>
#include <core/chrono.hpp>
#include <core/color.hpp>

#include <gpu/internal.hpp>

#include "shader/compute.h"

#include "scene_render_comp.hpp"

#define SCENE_NOISY_COMPUTE 0

void scene_renderer_init_compute(SceneRenderer* renderer)
{
    renderer->compute.pipeline = gpu_pipeline_create_compute(renderer->gpu, {
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .code = scene_render_comp,
        .entry = "main",
    });
}

void scene_render_compute(SceneRenderer* renderer, const SceneRenderInfo& info)
{
#if SCENE_NOISY_COMPUTE
    log_debug("scene_render_compute");
    log_debug("  viewport: {}", info.viewport);
#endif

    ankerl::unordered_dense::set<void*> reads;
    ankerl::unordered_dense::set<void*> writes{info.target};

    [[maybe_unused]] auto start = std::chrono::steady_clock::now();

    auto target_extent = vec_cast<f32>(info.target->base()->extent);
    auto target_bounds = aabb2f32{{}, target_extent, minmax};

#if SCENE_NOISY_COMPUTE
    log_debug("  target.extent: {}", info.target->base()->extent);
#endif

    auto pixel_size = target_extent / info.viewport.extent;
    auto layout_to_pixel = [&](vec2f32 layout) {
        return (layout - info.viewport.origin) * pixel_size;
    };

#if SCENE_NOISY_COMPUTE
    log_debug("  pixel.size: {}", pixel_size);
#endif

    struct FlattenedTexture
    {
        SceneTexture* texture;
        f32 opacity;
        aabb2f32 pixel_dst;
    };
    std::vector<FlattenedTexture> textures;

#if SCENE_NOISY_COMPUTE
    log_debug("  flattening scene tree");
#endif

    std::flat_set<i32> columns;
    std::flat_set<i32> rows;
    [&](this auto&& visit, SceneNode* node, vec2f32 translation, f32 opacity) -> void {
        scene_visit(node, OverloadSet {
            [&](SceneTexture* texture) {
                rect2f32 dst = texture->dst;
                dst.origin += translation;

                // log_debug("    texture");
                // log_debug("      dst:         {}", dst);

                aabb2f32 pixel_dst = dst;
                pixel_dst.min = layout_to_pixel(pixel_dst.min);
                pixel_dst.max = layout_to_pixel(pixel_dst.max);
                // log_debug("      pixel_dst:   {}", pixel_dst);

                aabb2f32 intersected;
                if (!aabb_intersects<f32>(target_bounds, pixel_dst, &intersected)) {
                    return;
                }
                // log_debug("      intersected: {}", intersected);

                textures.emplace_back(FlattenedTexture {
                    .texture = texture,
                    .opacity = opacity,
                    .pixel_dst = pixel_dst,
                });

                reads.emplace(texture->image.get());
                reads.emplace(texture->sampler.get());

                columns.insert(std::floor(intersected.min.x));
                columns.insert(std::ceil( intersected.min.x));
                columns.insert(std::floor(intersected.max.x));
                columns.insert(std::ceil( intersected.max.x));
                rows.insert(   std::floor(intersected.min.y));
                rows.insert(   std::ceil( intersected.min.y));
                rows.insert(   std::floor(intersected.max.y));
                rows.insert(   std::ceil( intersected.max.y));
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

// #if SCENE_NOISY_COMPUTE
//     log_debug("  columns: {}", columns);
//     log_debug("  rows: {}", rows);
// #endif

    if (columns.size() <= 1 || rows.size() <= 1) {
#if SCENE_NOISY_COMPUTE
        log_error("NOT ENOUGH COLS/ROWS ({}/{})", columns.size(), rows.size());
#endif
        return;
    }

    std::vector<std::pair<std::vector<SceneTexture*>, bool>> zones;
    zones.resize((columns.size() - 1) * (rows.size() - 1));
#if SCENE_NOISY_COMPUTE
    log_debug("  zones: {}", zones.size());
#endif

    for (auto& flattened : textures) {
        auto aabb = flattened.pixel_dst;

        aabb2f32 outer{vec_floor(aabb.min), vec_ceil( aabb.max), minmax};
        aabb2f32 inner{vec_ceil( aabb.min), vec_floor(aabb.max), minmax};

        for (usz row = 0; row < rows.size() - 1; ++row) {
            i32 y = rows.begin()[row];
            if (y >= outer.min.y && y < outer.max.y) {
                for (usz col = 0; col < columns.size() - 1; ++col) {
                    i32 x = columns.begin()[col];
                    if (x >= outer.min.x && x < outer.max.x) {
                        auto& zone = zones[row * (columns.size() - 1) + col];
                        if (!zone.second) {
                            zone.first.emplace_back(flattened.texture);

                            bool fully_covered = y >= inner.min.y && y < inner.max.y
                                              && x >= inner.min.x && x < inner.max.x;

                            if (fully_covered && !flattened.texture->image && flattened.texture->tint.w * flattened.opacity == 255) {
                                zone.second = true;
                            }
                        }
                    }
                }
            }
        }
    }

    std::flat_map<std::vector<SceneTexture*>, Region<i32>> unique_stacks;
    for (usz row = 0; row < rows.size() - 1; ++row) {
        for (usz col = 0; col < columns.size() - 1; ++col) {
            auto& zone = zones[row * (columns.size() - 1) + col];
            auto& stack = unique_stacks[zone.first];
            aabb2i32 aabb{{columns.begin()[col], rows.begin()[row]}, {columns.begin()[col + 1], rows.begin()[row + 1]}, minmax};
            region_op<RegionOpUnion>(stack, stack, RegionSingle<i32>{aabb});
        }
    }

#if SCENE_NOISY_COMPUTE
    log_debug("  unique stacks: {}", unique_stacks.size());
#endif

    struct SceneDispatch
    {
        u32 quad_offset;
        u32 quad_count;
        rect2i32 region;
    };

    std::vector<SceneQuad> quads;
    std::vector<SceneDispatch> dispatches;
    for (auto[stack, region] : unique_stacks) {
        u32 quad_offset = quads.size();
        for (auto& flattened : textures) {
            if (!std::ranges::contains(stack, flattened.texture)) continue;

            auto image   = flattened.texture->image.get()   ?: renderer->white.get();
            auto sampler = flattened.texture->sampler.get() ?: renderer->nearest.get();

            u32 flags = 0;
            if (flattened.texture->blend == GpuBlendMode::premultiplied) {
                flags |= SCENE_DRAW_FLAG_PREMULTIPLIED;
            }

            quads.emplace_back(SceneQuad {
                .dst = flattened.pixel_dst,
                .texture = {image, sampler},
                .src = flattened.texture->src,
                .tint = flattened.texture->tint,
                .flags = flags,
            });
        }

        // log_trace("   - {}", region.sections.size());
        for (auto& band : region.bands) {
            for (auto& section : std::span(region.sections).subspan(band.start, band.count)) {
                dispatches.emplace_back(SceneDispatch {
                    .quad_offset = quad_offset,
                    .quad_count = u32(stack.size()),
                    .region = {{section.min, band.min}, {section.max, band.max}, minmax},
                });
            }
        }
    }
#if SCENE_NOISY_COMPUTE
    log_debug("  quads:      {}", quads.size());
    log_debug("  dispatches: {}", dispatches.size());
#endif

    if (quads.empty()) {
#if SCENE_NOISY_COMPUTE
        log_warn("  QUADS EMPTY");
#endif
        return;
    }

    auto* gpu = renderer->gpu;

    // Upload quads
    auto gpu_rects = gpu_buffer_create(renderer->gpu, quads.size() * sizeof(SceneQuad), {});
    std::memcpy(gpu_rects->host_address, quads.data(), gpu_rects->size);
    reads.emplace(gpu_rects.get());

    gpu_use_resources(gpu, reads, writes);

    [[maybe_unused]] u32 accum_used_invocations = 0;
    [[maybe_unused]] u32 accum_total_invocations = 0;
    for (auto[i, dispatch] : dispatches | std::views::enumerate) {
        vec2i32 local_size = {};
        i32 best = INT_MAX;
        for (auto size : std::to_array<vec2i32>({{8, 8}, {4, 16}, {16, 4}, {2, 32}, {32, 2}, {1, 64}, {64, 1}})) {
            auto total_invocations = align_up_power2(dispatch.region.extent.x, size.x) * align_up_power2(dispatch.region.extent.y, size.y);
            if (total_invocations < best) {
                local_size = size;
                best = total_invocations;
            }
        }

        auto used_invocations  = dispatch.region.extent.x * dispatch.region.extent.y;
        auto total_invocations = align_up_power2(dispatch.region.extent.x, local_size.x) * align_up_power2(dispatch.region.extent.y, local_size.y);

        accum_used_invocations += used_invocations;
        accum_total_invocations += total_invocations;

        // log_trace("  dispatch[{}] = {} --> {} ({}/{} {:.2f}% - {} wasted invocations)", i, dispatch.region, local_size, used_invocations, total_invocations, (100.f * used_invocations) / total_invocations, total_invocations - used_invocations);

        gpu_dispatch(gpu, renderer->compute.pipeline.get(),
            {u32(dispatch.region.extent.x + 7) / 8, u32(dispatch.region.extent.y + 7) / 8, 1},
            view_bytes(SceneComputeInput {
                .quad_stack = gpu_rects->device<SceneQuad>() + dispatch.quad_offset,
                .quad_count = dispatch.quad_count,
                .region = dispatch.region,
                .target = {info.target, renderer->nearest.get()},
                .debug_color = color_hsv_to_rgb({i / f32(dispatches.size() - 1), 1.f, 1.f, 1.f})
            }));
    }

#if SCENE_NOISY_COMPUTE
    log_warn("  ({}/{} {:.2f}% - {} wasted invocations)", accum_used_invocations, accum_total_invocations, (100.f * accum_used_invocations) / accum_total_invocations, accum_total_invocations - accum_used_invocations);

    [[maybe_unused]] auto end = std::chrono::steady_clock::now();
    log_warn("  recorded {} zones in {}", zones.size(), FmtDuration{end - start});
#endif

}
