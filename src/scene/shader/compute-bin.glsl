#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_debug_printf : enable

#include "render.h"

layout(push_constant, scalar) uniform PushConstants { SceneComputeBinPassInput pc; };

layout(local_size_x = SCENE_COMPUTE_BIN_PASS_LOCAL_SIZE,
       local_size_y = SCENE_COMPUTE_BIN_PASS_LOCAL_SIZE, local_size_z = 1) in;
void main()
{
    if (gl_GlobalInvocationID.x >= pc.extent.x || gl_GlobalInvocationID.y >= pc.extent.y) return;

    GPU_PTR(SceneComputeBin) bin = pc.bins[  gl_GlobalInvocationID.y * pc.row_stride
                                           + gl_GlobalInvocationID.x
                                           + SCENE_RESERVED_BIN_COUNT];

    aabb2f32 tile;
    tile.min = vec2f32(gl_GlobalInvocationID.xy) * vec2f32(SCENE_BIN_SIZE);
    tile.max = tile.min + vec2f32(SCENE_BIN_SIZE);

    u32 slot = 0;
    for (u32 i = 1; i < pc.quad_count; ++i) {
        aabb2f32 bounds = pc.quad_bounds[i]._;
        aabb2f32 inner = aabb2f32(max(tile.min, bounds.min), min(tile.max, bounds.max));

        if (inner.max.x <= inner.min.x || inner.max.y <= inner.min.y) continue;

        if (slot >= SCENE_QUADS_PER_BIN) {
            u32 next_bin = atomicAdd(pc.bins[0]._.next_bin, 1);
            if (next_bin >= pc.bin_count) {
                // All bin allocations have been exhausted
                break;
            }
            bin._.next_bin = next_bin;
            bin = pc.bins[next_bin];
            slot = 0;
        }

        bin._.quads[slot] = SCENE_QUAD_INDEX_TYPE(i);
        slot += 1;

        if (pc.quad_opaque_flags[i]._ == 1 && tile == inner) {
            // Early out if quad is opaque and covers tile
            break;
        }
    }

    if (slot < SCENE_QUADS_PER_BIN) {
        bin._.quads[slot] = SCENE_QUAD_INDEX_TYPE(0);
    }
    bin._.next_bin = 0;
}
