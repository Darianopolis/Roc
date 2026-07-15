#version 460
#extension GL_GOOGLE_include_directive : require

#include "render.h"

layout(push_constant, scalar) uniform PushConstants { SceneRenderInput pc; };

layout(location = 0)      in vec2f32 in_uv;
layout(location = 1) flat in     u32 in_quad;

layout(location = 0) out vec4f32 out_color;

void main()
{
    SceneQuad quad = pc.quads.data[in_quad];

    vec4f32 tint = unpack_unorm4u8(quad.tint);
    vec4f32 color = gpu_image_sample(quad.texture, in_uv) * tint;

    if ((quad.flags & SCENE_DRAW_FLAG_PREMULTIPLIED) == 0) {
        color.rgb *= color.a;
    }

    out_color = color;
}
