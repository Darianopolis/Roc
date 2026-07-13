#version 460
#extension GL_GOOGLE_include_directive : require

#include "compute.h"

layout(push_constant, scalar) uniform PushConstants { SceneComputeInput pc; };

layout (local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
void main()
{
    if (   gl_GlobalInvocationID.x >= pc.region.extent.x
        || gl_GlobalInvocationID.y >= pc.region.extent.y) return;

    vec2i32 pos = vec2i32(gl_GlobalInvocationID.xy) + pc.region.origin;

    vec4f32 color = vec4f32(0, 0, 0, 0);
    for (u32 i = 0; i < pc.quad_count; ++i) {
        SceneQuad quad = pc.quad_stack.data[i];

        vec4f32 tint = unpack_unorm4u8(quad.tint);

        // Sample source pixel
        vec2f32 uv = (pos + vec2f32(0.5) - quad.dst.origin) / quad.dst.extent;
        vec4f32 sampled = gpu_image_sample_grad(quad.texture, uv, vec2f32(1, 0), vec2f32(0, 1)) * tint;

        // Convert to linear
        sampled.rgb = srgb_eotf(sampled.rgb);

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

        // if (sampled.a != 1) color = vec4f32(1, 0, 0, 1);
        // break;

        // if (mod(quad.dst.origin, vec2f32(1, 1)) != vec2f32(0, 0)) {
        //     if (       pos.x < ceil(quad.dst.origin.x) || pos.x >= floor(quad.dst.origin.x + quad.dst.extent.x)
        //             || pos.y < ceil(quad.dst.origin.y) || pos.y >= floor(quad.dst.origin.y + quad.dst.extent.y)) {
        //         color = vec4f32(1, 0, 0, 1);
        //     }
        // }
    }

    color.rgb = srgb_oetf(color.rgb);

    color = mix(color, pc.debug_color, 0.5);

    gpu_image_store(pc.target, pos, color);

    // gpu_image_store(pc.target, pos, pc.debug_color);
    // gpu_image_store(pc.target, pos, vec4f32(1, 0, 0, 1));
}
