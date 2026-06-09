#version 460
#extension GL_GOOGLE_include_directive : require

#include "render.h"

layout(push_constant, scalar) uniform PushConstants { SceneRenderInput pc; };

layout(location = 0)      out vec2f32 out_uv;
layout(location = 1) flat out     u32 out_rect;

const vec2f32 positions[] = {
    vec2f32(0, 0), vec2f32(1, 0),
    vec2f32(0, 1), vec2f32(1, 1),
};

void main()
{
    SceneQuad quad = pc.quads.data[gl_InstanceIndex];

    vec2f32 local_pos = positions[gl_VertexIndex];

    vec2f32 pixel_pos = fma(local_pos, quad.dst.extent, quad.dst.origin);
    gl_Position = vec4f32(fma(pixel_pos, pc.scale, pc.offset), 0, 1);

    out_uv = fma(local_pos, quad.src.extent, quad.src.origin);
    out_rect = gl_InstanceIndex;
}
