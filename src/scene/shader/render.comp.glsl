#version 460
#extension GL_GOOGLE_include_directive : require

#include "common.glsl"

layout(push_constant, scalar) uniform PushConstants { SceneRenderComputeInput pc; };

layout(local_size_x = SCENE_COMPUTE_DISPATCH_SIZE, local_size_y = SCENE_COMPUTE_DISPATCH_SIZE, local_size_z = 1) in;
void main()
{
    if (gl_GlobalInvocationID.x >= pc.extent.x || gl_GlobalInvocationID.y >= pc.extent.y) return;

    GPU_CONST_PTR(SceneRenderBin) bin = pc.bins[  (gl_GlobalInvocationID.y / SCENE_BIN_SIZE) * pc.row_stride
                                                + (gl_GlobalInvocationID.x / SCENE_BIN_SIZE)
                                                + SCENE_RESERVED_BIN_COUNT];

    vec4f32 color = vec4f32(0, 0, 0, 0);

    vec2f32 pos = vec2f32(gl_GlobalInvocationID.xy);

    u32 slot = 0;
    while (true) {
        u32 quad_id = bin._.quads[slot];
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

        slot += 1;
        if (slot == SCENE_QUADS_PER_BIN) {
            if (bin._.next_bin != 0) {
                slot = 0;
                bin = pc.bins[bin._.next_bin];
            } else {
                break;
            }
        }
    }

    color.rgb = srgb_oetf(color.rgb);

    gpu_image_store(pc.target, vec2i32(gl_GlobalInvocationID.xy), color);
}
