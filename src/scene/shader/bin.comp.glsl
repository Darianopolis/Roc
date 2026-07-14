#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_debug_printf : enable

#include "render.h"

layout(push_constant, scalar) uniform PushConstants { SceneRenderBinInput pc; };

layout(local_size_x = SCENE_BIN_DISPATCH_SIZE, local_size_y = SCENE_BIN_DISPATCH_SIZE, local_size_z = 1) in;
void main()
{
    if (gl_GlobalInvocationID.x >= pc.extent.x || gl_GlobalInvocationID.y >= pc.extent.y) return;

    u32 bin_index = gl_GlobalInvocationID.y * pc.row_stride + gl_GlobalInvocationID.x + 1;

    vec2f32 tl = vec2f32(gl_GlobalInvocationID.xy) * vec2f32(SCENE_BIN_SIZE);
    vec2f32 br = tl + vec2f32(SCENE_BIN_SIZE);

    u32 slot = 0;
    for (u32 i = pc.quad_count; i --> 1;) {
        aabb2f32 bounds = pc.quad_bounds.data[i];
        vec2f32 inner_tl = vec2f32(max(tl.x, bounds.min.x), max(tl.y, bounds.min.y));
        vec2f32 inner_br = vec2f32(min(br.x, bounds.max.x), min(br.y, bounds.max.y));

        if (inner_br.x <= inner_tl.x || inner_br.y <= inner_tl.y) continue;

        if (slot >= SCENE_QUADS_PER_BIN) {
            u32 next_bin = atomicAdd(pc.bins.data[0].next_bin, 1);
            if (next_bin >= pc.bin_count) {
                // All bin allocations have been exhausted
                break;
            }
            pc.bins.data[bin_index].next_bin = next_bin;
            bin_index = next_bin;
            slot = 0;
        }

        pc.bins.data[bin_index].quads[slot] = i;
        slot += 1;

        if (pc.quad_opaque_flags.data[i] == 1 && tl == inner_tl && br == inner_br) {
            // Early out if quad is opaque and covers tile
            break;
        }
    }

    if (slot < SCENE_QUADS_PER_BIN) {
        pc.bins.data[bin_index].quads[slot] = 0;
    }
    pc.bins.data[bin_index].next_bin = 0;
}
