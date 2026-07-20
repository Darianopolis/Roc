#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_debug_printf : enable

#include "render.glsl"

layout(push_constant, scalar) uniform PushConstants { SceneCompute2PixelPassInput pc; };

layout(local_size_x = SCENE_COMPUTE2_PIXEL_PASS_LOCAL_SIZE,
       local_size_y = SCENE_COMPUTE2_PIXEL_PASS_LOCAL_SIZE, local_size_z = 1) in;
void main()
{
    if (gl_GlobalInvocationID.x >= pc.extent.x || gl_GlobalInvocationID.y >= pc.extent.y) return;

    // Get coarse bin metadata

    GPU_CONST_PTR(u16) fine_bin;
    u32 max_depth;
    {
        u32 coarse_bin_index = (gl_GlobalInvocationID.y / SCENE_COARSE_BIN_SIZE) * pc.coarse_bin_row_stride
                            + (gl_GlobalInvocationID.x / SCENE_COARSE_BIN_SIZE)
                            + SCENE_RESERVED_COARSE_BIN_COUNT;

        GPU_CONST_PTR(SceneComputeCoarseBinInfo) coarse_bin_info = pc.coarse_bin_infos[coarse_bin_index];
        max_depth = coarse_bin_info._.depth;

        // Compute fine bin input locations

        u32 fine_bin_index = (((gl_GlobalInvocationID.y / SCENE_FINE_BIN_SIZE) % SCENE_COARSE_FINE_BIN_RATIO) * SCENE_COARSE_FINE_BIN_RATIO)
                            + ((gl_GlobalInvocationID.x / SCENE_FINE_BIN_SIZE) % SCENE_COARSE_FINE_BIN_RATIO);
        fine_bin = pc.fine_bins[coarse_bin_info._.offset + fine_bin_index * coarse_bin_info._.depth];
    }

    // Evaluate

    vec4f32 color = vec4f32(0, 0, 0, 0);

    vec2f32 pos = vec2f32(gl_GlobalInvocationID.xy);

    for (u32 i = 0; i < max_depth; ++i) {
        u32 quad_id = fine_bin[i]._;
        if (quad_id == 0) break;

        aabb2f32 bounds = pc.quad_bounds[quad_id]._;
        if (       pos.x >= bounds.min.x && pos.x < bounds.max.x
                && pos.y >= bounds.min.y && pos.y < bounds.max.y) {
            GPU_CONST_PTR(SceneQuad) quad = pc.quads[quad_id];

            vec2f32 uv = (pos + vec2f32(0.5) - quad._.dst.origin) / quad._.dst.extent;
                    uv = fma(uv, quad._.src.extent, quad._.src.origin);

            // Blend
            color += quad_sample(quad, uv) * (1.f - color.w);

            // Early stop
            if (color.a >= 1) break;
        }
    }

    color.rgb = srgb_oetf(color.rgb);

    gpu_image_store(pc.target, vec2i32(gl_GlobalInvocationID.xy), color);
}
