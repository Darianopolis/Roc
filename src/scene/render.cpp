#include "internal.hpp"

#include <core/math.hpp>
#include <core/color.hpp>
#include <core/log.hpp>

#include <gpu/internal.hpp>

#include "shader/render.h"

#include "scene_raster_fragment.hpp"
#include "scene_raster_vertex.hpp"
#include "scene_raster_output.hpp"

#include "scene_compute_bin.hpp"
#include "scene_compute_pixel.hpp"

#include "scene_compute2_bin.hpp"
#include "scene_compute2_pixel.hpp"

static constexpr auto scene_blend_format = VK_FORMAT_R16G16B16A16_SFLOAT;

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

    renderer->pool = gpu_image_pool_create(renderer->gpu);

    renderer->raster_blend = gpu_pipeline_create(renderer->gpu, {
        .format = gpu_format_from_vulkan(scene_blend_format),
        .shaders = {{
            {
                .stage = VK_SHADER_STAGE_VERTEX_BIT,
                .code  = scene_raster_vertex,
                .entry = "main",
            },
            {
                .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                .code  = scene_raster_fragment,
                .entry = "main",
            }
        }},
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .blend_direction = GpuBlendDirection::front_to_back,
    });

    renderer->raster_output = gpu_pipeline_create_compute(gpu, {
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .code = scene_raster_output,
        .entry = "main",
    });

    renderer->compute_bin = gpu_pipeline_create_compute(gpu, {
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .code = scene_compute_bin,
        .entry = "main",
    });

    renderer->compute_pixel = gpu_pipeline_create_compute(gpu, {
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .code = scene_compute_pixel,
        .entry = "main",
    });

    renderer->compute2_bin = gpu_pipeline_create_compute(gpu, {
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .code = scene_compute2_bin,
        .entry = "main",
    });

    renderer->compute2_pixel = gpu_pipeline_create_compute(gpu, {
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .code = scene_compute2_pixel,
        .entry = "main",
    });

    return renderer;
}

static
void render_compute2(SceneRenderer* renderer, const SceneRenderInfo& info,
    vec2u32 extent,
    std::span<const SceneQuad> quads,
    std::span<const aabb2f32> quad_bounds,
    std::span<const u8> quad_opaque_flags,
    std::flat_set<GpuResource*>& reads)
{
    // auto start = std::chrono::steady_clock::now();

    usz quad_count = quad_bounds.size();

    auto coarse_bin_counts = (extent + literal_cast<u32>(SCENE_COARSE_BIN_SIZE) - 1u) / literal_cast<u32>(SCENE_COARSE_BIN_SIZE);
    auto coarse_bin_count = coarse_bin_counts.x * coarse_bin_counts.y + SCENE_RESERVED_COARSE_BIN_COUNT;

    std::vector<u16> coarse_bin_next_slot;
    std::vector<u32> coarse_bin_remaps;
    std::vector<SceneComputeBin> coarse_bins;
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
        vec2i32 tile_start = vec_cast<i32>(vec_floor(quad_bounds[i].min / literal_cast<f32>(SCENE_COARSE_BIN_SIZE)));
        vec2i32 tile_end   = vec_cast<i32>(vec_ceil( quad_bounds[i].max / literal_cast<f32>(SCENE_COARSE_BIN_SIZE)));
        tile_start = vec_max(tile_start, vec2i32{});
        tile_end   = vec_min(tile_end,   vec_cast<i32>(coarse_bin_counts));

        for (i32 tile_y = tile_start.y; tile_y < tile_end.y; ++tile_y) {
            u32 tile_row_index = num_cast<u32>(tile_y) * coarse_bin_counts.x + 1;
            for (i32 tile_x = tile_start.x; tile_x < tile_end.x; ++tile_x) {
                u32 original_tile = tile_row_index + num_cast<u32>(tile_x);

                usz tile = coarse_bin_remaps[original_tile];
                if (coarse_bin_next_slot[original_tile] >= SCENE_QUADS_PER_BIN) [[unlikely]] {
                    u32 new_tile = allocate_coarse_bin();        // Allocate a new bin
                    coarse_bin_remaps[original_tile] = new_tile; // Point the head of the stack to the new tile
                    coarse_bin_next_slot[original_tile] = 0;     // Reset the bin slot
                    coarse_bins[tile].next_bin = new_tile;       // Update the next bin link
                    tile = new_tile;                             // Update the tile location
                }
                coarse_bins[tile].quads[coarse_bin_next_slot[original_tile]++] = num_cast<u16>(i);
            }
        }
    }

    std::vector<SceneComputeCoarseBinInfo> coarse_bin_infos;
    coarse_bin_infos.resize(coarse_bin_count);

    // auto end = std::chrono::steady_clock::now();
    // log_warn("Filled {} bins in {} (quads: {})", coarse_bin_count, FmtDuration{end - start}, quad_count);
    // log_warn("  coarse bins: {}", coarse_bin_counts);
    // usz coarse_bin_slabs_used = 0;
    u32 fine_bin_slots = 0;
    std::flat_map<u32, u32> histogram;
    {
        for (u32 tile = 1; tile < coarse_bin_count; ++tile) {
            // coarse_bin_slabs_used++;
            u32 depth = coarse_bin_next_slot[tile];
            u32 next = tile;
            while ((next = coarse_bins[next].next_bin)) {
                // coarse_bin_slabs_used++;
                depth += SCENE_QUADS_PER_BIN;
            }
            histogram[depth]++;

            coarse_bin_infos[tile] = {
                .offset = fine_bin_slots,
                .depth = num_cast<u16>(depth),
            };
            // log_warn("coarse bin {:3}, fine offset: {} (expected: {})", tile, coarse_bin_infos[tile].offset, fine_bin_slots);
            fine_bin_slots += SCENE_COARSE_FINE_BIN_RATIO * SCENE_COARSE_FINE_BIN_RATIO * depth;
            // log_warn("coarse bin {:3}, fine offset: {}, depth: {}", tile, coarse_bin_infos[tile].offset, coarse_bin_infos[tile].depth);

        }
    }
    // log_warn("  slabs: {}", coarse_bin_slabs_used);
    // log_warn("  slab depths");
    // for (auto[depth, count] : histogram) {
    //     log_warn("    {:3}: {}", depth, count);
    // }
    // log_warn("  fine_bin_slots: {} ({})", fine_bin_slots, FmtBytes{fine_bin_slots * sizeof(SCENE_QUAD_INDEX_TYPE)});
    // usz naive_fine_bin_slots = quad_count * ((extent.x + SCENE_FINE_BIN_SIZE - 1) / SCENE_FINE_BIN_SIZE)
    //                                       * ((extent.y + SCENE_FINE_BIN_SIZE - 1) / SCENE_FINE_BIN_SIZE);
    // log_warn("  naive_fine_bin_slots: {} ({})", naive_fine_bin_slots, FmtBytes{naive_fine_bin_slots * sizeof(SCENE_QUAD_INDEX_TYPE)});

#if 1
    auto* gpu = renderer->gpu;

    auto cmd = gpu_record(gpu);

    auto copy_to_gpu = [&]<typename T>(std::span<const T> elements) {
        auto buffer = gpu_buffer_create(gpu, elements.size() * sizeof(T), {});
        std::memcpy(buffer->host_address, elements.data(), buffer->size);
        gpu_protect(cmd, buffer);
        return buffer;
    };

    auto gpu_quad_bounds = copy_to_gpu(quad_bounds);
    auto gpu_quad_opaque_flags = copy_to_gpu(quad_opaque_flags);

    auto gpu_quads = copy_to_gpu(quads);

    // 2. Bin

    auto gpu_coarse_bins = copy_to_gpu.operator()<SceneComputeBin>(coarse_bins);
    auto gpu_coarse_bin_infos = copy_to_gpu.operator()<SceneComputeCoarseBinInfo>(coarse_bin_infos);

    auto gpu_fine_bins = gpu_buffer_create(gpu, fine_bin_slots * sizeof(SCENE_QUAD_INDEX_TYPE), {});
    gpu_protect(cmd, gpu_fine_bins);

    gpu_barrier(cmd, reads, {{gpu_fine_bins.get()}});

    vec2u32 fine_bin_counts = coarse_bin_counts * literal_cast<u32>(SCENE_COARSE_FINE_BIN_RATIO);

    gpu_bind_pipeline(cmd, renderer->compute2_bin.get());
    gpu_push_constants(cmd, 0, view_bytes(SceneCompute2BinPassInput {
        .quad_bounds = gpu_quad_bounds->device<aabb2f32>(),
        .quad_opaque_flags = gpu_quad_opaque_flags->device<u8>(),
        .coarse_bins = gpu_coarse_bins->device<SceneComputeBin>(),
        .coarse_bin_infos = gpu_coarse_bin_infos->device<SceneComputeCoarseBinInfo>(),
        .fine_bins = gpu_fine_bins->device<SCENE_QUAD_INDEX_TYPE>(),
        .coarse_bin_row_stride = coarse_bin_counts.x,
        .extent = fine_bin_counts,
    }));

    gpu_dispatch(cmd, vec_join((fine_bin_counts + literal_cast<u32>(SCENE_COMPUTE2_BIN_PASS_LOCAL_SIZE - 1))
                                                / literal_cast<u32>(SCENE_COMPUTE2_BIN_PASS_LOCAL_SIZE), 1u));

    // 3. Draw

    gpu_barrier(cmd, {{gpu_fine_bins.get()}}, {{info.target}});

    gpu->vk.CmdPipelineBarrier2(cmd->buffer, ptr_to(VkDependencyInfo {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .memoryBarrierCount = 1,
        .pMemoryBarriers = ptr_to(VkMemoryBarrier2 {
            .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            .srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            .dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT,
        }),
    }));

    gpu_bind_pipeline(cmd, renderer->compute2_pixel.get());
    gpu_push_constants(cmd, 0, view_bytes(SceneCompute2PixelPassInput {
        .coarse_bin_infos = gpu_coarse_bin_infos->device<SceneComputeCoarseBinInfo>(),
        .fine_bins = gpu_fine_bins->device<SCENE_QUAD_INDEX_TYPE>(),
        .quad_bounds = gpu_quad_bounds->device<aabb2f32>(),
        .quads = gpu_quads->device<SceneQuad>(),
        .coarse_bin_row_stride = coarse_bin_counts.x,
        .target = info.target,
        .extent = extent,
    }));

    gpu_dispatch(cmd, vec_join((extent + literal_cast<u32>(SCENE_COMPUTE2_PIXEL_PASS_LOCAL_SIZE - 1))
                                       / literal_cast<u32>(SCENE_COMPUTE2_PIXEL_PASS_LOCAL_SIZE), 1u));
#endif
}

void scene_render(SceneRenderer* renderer, const SceneRenderInfo& info)
{
    debug_assert(info.target->base()->usage.contains(GpuImageUsage::storage));

    // bool use_compute = info.options.contains(SceneRenderOption::use_compute);

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
        // if (use_compute && quads.size() >= std::numeric_limits<SCENE_QUAD_INDEX_TYPE>::max()) return;
        if (quads.size() >= std::numeric_limits<SCENE_QUAD_INDEX_TYPE>::max()) return;

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
            flags |= SCENE_DRAW_FLAG_PREMULTIPLIED;
        }

        if (!texture->image && tint.w == 1.f) {
            flags |= SCENE_DRAW_FLAG_OPAQUE;
        }

        quads.emplace_back(SceneQuad {
            .dst = pixel_dst,
            .texture = {image, sampler},
            .src = texture->src,
            .tint = pack_unorm<u8>(srgb_oetf(tint)),
            .flags = flags,
        });
        quad_bounds.emplace_back(pixel_dst);
        quad_opaque_flags.emplace_back((flags & SCENE_DRAW_FLAG_OPAQUE) != 0);

        reads.insert(image);
        reads.insert(sampler);
    };

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
                quads.emplace_back(SceneQuad {
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

    if (info.method == SceneRenderMethod::compute2) {
        render_compute2(renderer, info, extent, quads, quad_bounds, quad_opaque_flags, reads);
        return;
    }

    auto* gpu = renderer->gpu;

    auto cmd = gpu_record(gpu);

    auto copy_to_gpu = [&]<typename T>(const std::vector<T>& elements) {
        auto buffer = gpu_buffer_create(gpu, elements.size() * sizeof(T), {});
        std::memcpy(buffer->host_address, elements.data(), buffer->size);
        gpu_protect(cmd, buffer);
        return buffer;
    };

    auto gpu_quads = copy_to_gpu(quads);

    if (info.method == SceneRenderMethod::compute) {
        auto gpu_quad_bounds = copy_to_gpu(quad_bounds);
        auto gpu_quad_opaque_flags = copy_to_gpu(quad_opaque_flags);

        // 2. Bin

        static constexpr auto extra_bin_layers = 3;

        auto bin_counts = (extent + literal_cast<u32>(SCENE_BIN_SIZE - 1)) / literal_cast<u32>(SCENE_BIN_SIZE);
        auto bin_count = bin_counts.x * bin_counts.y + SCENE_RESERVED_BIN_COUNT;
        auto extra_bins_start = bin_count;
        bin_count += (bin_counts.x * bin_counts.y) * extra_bin_layers;

        auto gpu_bins = gpu_buffer_create(gpu, bin_count * sizeof(SceneComputeBin), {});
        gpu_protect(cmd, gpu_bins);
        // We reserve the first bin for our atomic bump allocator state
        gpu_bins->host<SceneComputeBin>()->next_bin = extra_bins_start;

        gpu_barrier(cmd, reads, {{gpu_bins.get()}});

        gpu_bind_pipeline(cmd, renderer->compute_bin.get());
        gpu_push_constants(cmd, 0, view_bytes(SceneComputeBinPassInput {
            .quad_bounds = gpu_quad_bounds->device<aabb2f32>(),
            .quad_opaque_flags = gpu_quad_opaque_flags->device<u8>(),
            .quad_count = num_cast<u32>(quads.size()),
            .bins = gpu_bins->device<SceneComputeBin>(),
            .bin_count = bin_count,
            .row_stride = bin_counts.x,
            .extent = {bin_counts.x, bin_counts.y},
        }));

        gpu_dispatch(cmd, vec_join((bin_counts + literal_cast<u32>(SCENE_COMPUTE_BIN_PASS_LOCAL_SIZE - 1))
                                               / literal_cast<u32>(SCENE_COMPUTE_BIN_PASS_LOCAL_SIZE), 1u));

        // 3. Draw

        gpu_barrier(cmd, {{gpu_bins.get()}}, {{info.target}});

        gpu_bind_pipeline(cmd, renderer->compute_pixel.get());
        gpu_push_constants(cmd, 0, view_bytes(SceneComputePixelPassInput {
            .quad_bounds = gpu_quad_bounds->device<aabb2f32>(),
            .quads = gpu_quads->device<SceneQuad>(),
            .bins = gpu_bins->device<SceneComputeBin>(),
            .row_stride = bin_counts.x,
            .target = info.target,
            .extent = extent,
        }));

        gpu_dispatch(cmd, vec_join((extent + literal_cast<u32>(SCENE_COMPUTE_PIXEL_PASS_LOCAL_SIZE - 1))
                                           / literal_cast<u32>(SCENE_COMPUTE_PIXEL_PASS_LOCAL_SIZE), 1u));
    } else {
        // 2. Accumulate

        auto stencil = renderer->pool->acquire({
            .extent = extent,
            .format = gpu_format_from_vulkan(VK_FORMAT_S8_UINT),
            .usage = GpuImageUsage::stencil,
        });

        auto blend = renderer->pool->acquire({
            .extent = extent,
            .format = gpu_format_from_vulkan(scene_blend_format),
            .usage = GpuImageUsage::render | GpuImageUsage::storage,
        });

        gpu_barrier(cmd, reads, {{stencil.get(), blend.get()}});

        gpu_begin_rendering(cmd, {
            .target = blend.get(),
            .stencil = stencil.get(),
            .clear_color = {{0,0,0,0}},
        });

        gpu_set_viewports(cmd, {{{{}, vec_cast<f32>(extent), xywh}}});
        gpu_set_scissors( cmd, {{{{}, vec_cast<i32>(extent), xywh}}});

        gpu_bind_pipeline(cmd, renderer->raster_blend.get());
        gpu_bind_index_buffer(cmd, renderer->indices.get(), 0, VK_INDEX_TYPE_UINT32);

        gpu_push_constants(cmd, 0, view_bytes(SceneRasterBlendPassInput {
            .quads = gpu_quads->device<SceneQuad>() + 1,
            .offset = -vec2f32(1.f, 1.f),
            .scale = 2.f / vec_cast<f32>(extent),
        }));

        gpu_draw_indexed(cmd, {
            .index_count = 6,
            .instance_count = num_cast<u32>(quads.size() - 1),
        });

        gpu_end_rendering(cmd);

        // 3. Output

        gpu_barrier(cmd, {{blend.get()}}, {{info.target}});

        gpu_bind_pipeline(cmd, renderer->raster_output.get());
        gpu_push_constants(cmd, 0, view_bytes(SceneRasterOutputPassInput {
            .source = blend.get(),
            .target = info.target,
            .extent = extent,
        }));

        gpu_dispatch(cmd, vec_join((extent + literal_cast<u32>(SCENE_RASTER_OUTPUT_PASS_LOCAL_SIZE - 1))
                                           / literal_cast<u32>(SCENE_RASTER_OUTPUT_PASS_LOCAL_SIZE), 1u));
    }
}
