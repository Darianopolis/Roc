#version 460
#extension GL_GOOGLE_include_directive : require

#include "common.glsl"

layout(push_constant, scalar) uniform PushConstants { SceneRenderInput pc; };

layout(location = 0)      in vec2f32 in_uv;
layout(location = 1) flat in     u32 in_quad;

layout(location = 0) out vec4f32 out_color;

void main()
{
    out_color = quad_sample(pc.quads[in_quad], in_uv);
}
