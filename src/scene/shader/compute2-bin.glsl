#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_debug_printf : enable

#include "render.glsl"

layout(push_constant, scalar) uniform PushConstants { SceneCompute2BinPassInput pc; };

layout(local_size_x = SCENE_COMPUTE2_BIN_PASS_LOCAL_SIZE,
       local_size_y = SCENE_COMPUTE2_BIN_PASS_LOCAL_SIZE, local_size_z = 1) in;
void main()
{
    if (gl_GlobalInvocationID.x >= pc.extent.x || gl_GlobalInvocationID.y >= pc.extent.y) return;

    // Get coarse bin inputs

    u32 coarse_bin_index = (gl_GlobalInvocationID.y / SCENE_COARSE_FINE_BIN_RATIO) * pc.coarse_bin_row_stride
                         + (gl_GlobalInvocationID.x / SCENE_COARSE_FINE_BIN_RATIO)
                         + SCENE_RESERVED_COARSE_BIN_COUNT;

    GPU_CONST_PTR(SceneComputeBin)           coarse_bin      = pc.coarse_bins[     coarse_bin_index];
    GPU_CONST_PTR(SceneComputeCoarseBinInfo) coarse_bin_info = pc.coarse_bin_infos[coarse_bin_index];

    // Compute fine bin output locations

    u32 fine_bin_index = (gl_LocalInvocationID.y * SCENE_COARSE_FINE_BIN_RATIO) + gl_LocalInvocationID.x;
    GPU_PTR(u16) fine_bin = pc.fine_bins[coarse_bin_info._.offset + fine_bin_index * coarse_bin_info._.depth];

    // Fine bin bounds

    aabb2f32 tile;
    tile.min = vec2f32(gl_GlobalInvocationID.xy) * vec2f32(SCENE_FINE_BIN_SIZE);
    tile.max = tile.min + vec2f32(SCENE_FINE_BIN_SIZE);

    u32 in_slot = 0;
    u32 out_slot = 0;
    while (true) {
        u32 quad_id = coarse_bin._.quads[in_slot];
        if (quad_id == 0) break;

        {
            aabb2f32 bounds = pc.quad_bounds[quad_id]._;
            aabb2f32 inner = aabb2f32(max(tile.min, bounds.min), min(tile.max, bounds.max));

            if (inner.max.x > inner.min.x && inner.max.y > inner.min.y) {
                fine_bin[out_slot]._ = SCENE_QUAD_INDEX_TYPE(quad_id);
                out_slot += 1;

                if (pc.quad_opaque_flags[quad_id]._ == 1 && tile == inner) {
                    // Early out if quad is opaque and covers tile
                    break;
                }
            }
        }

        in_slot += 1;
        if (in_slot == SCENE_QUADS_PER_BIN) {
            if (coarse_bin._.next_bin != 0) {
                in_slot = 0;
                coarse_bin = pc.coarse_bins[coarse_bin._.next_bin];
            } else {
                break;
            }
        }
    }

    if (out_slot < coarse_bin_info._.depth) {
        fine_bin[out_slot]._ = SCENE_QUAD_INDEX_TYPE(0);
    }
}
