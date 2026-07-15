#version 460
#extension GL_GOOGLE_include_directive : require

#include "render.h"

layout(push_constant, scalar) uniform PushConstants { SceneRenderComputeInput pc; };

layout(local_size_x = SCENE_COMPUTE_DISPATCH_SIZE, local_size_y = SCENE_COMPUTE_DISPATCH_SIZE, local_size_z = 1) in;
void main()
{
    if (gl_GlobalInvocationID.x >= pc.extent.x || gl_GlobalInvocationID.y >= pc.extent.y) return;

    u32 bin_index = (gl_GlobalInvocationID.y / SCENE_BIN_SIZE) * pc.row_stride + (gl_GlobalInvocationID.x / SCENE_BIN_SIZE) + 1;
    SceneRenderBin bin = pc.bins.data[bin_index];

    vec4f32 color = vec4f32(0, 0, 0, 0);

    vec2f32 pos = vec2f32(gl_GlobalInvocationID.xy);

    u32 slot = 0;
    while (true) {
        u32 quad_id = bin.quads[slot];
        if (quad_id == 0) break;

        aabb2f32 bounds = pc.quad_bounds.data[quad_id];
        if (       pos.x >= bounds.min.x && pos.x < bounds.max.x
                && pos.y >= bounds.min.y && pos.y < bounds.max.y) {
            SceneQuad quad = pc.quads.data[quad_id];

            // Sample
            vec2f32 uv = (pos + vec2f32(0.5) - quad.dst.origin) / quad.dst.extent;
            uv = uv * quad.src.extent + quad.src.origin;
            vec4f32 sampled = gpu_image_sample(quad.texture, uv);

            // Tint
            vec4f32 tint = unpack_unorm4u8(quad.tint);
            sampled *= tint;

            // Premultiply
            if ((quad.flags & SCENE_DRAW_FLAG_PREMULTIPLIED) == 0) {
                sampled.rgb *= sampled.a;
            }

            // Blend
            vec4f32 src = color;
            vec4f32 dst = sampled;
            color = src + dst * (1.f - src.w);

            // Early stop
            if (color.a == 1) break;
        }

        slot += 1;
        if (slot == SCENE_QUADS_PER_BIN) {
            if (bin.next_bin != 0) {
                slot = 0;
                bin = pc.bins.data[bin.next_bin];
            } else {
                break;
            }
        }
    }

    gpu_image_store(pc.target, vec2i32(gl_GlobalInvocationID.xy), color);
}
