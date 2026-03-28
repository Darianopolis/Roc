#version 460
#extension GL_GOOGLE_include_directive : require

#include "render.h"

layout(push_constant, scalar) uniform PushConstants { SceneRenderInput si; };

layout(location = 0) out vec2f32 out_uv;
layout(location = 1) out vec4f32 out_color;

void main()
{
    SceneVertex v = si.vertices.data[gl_VertexIndex];
    gl_Position = vec4f32(fma(v.pos, si.scale, si.offset), 0.0, 1.0);
    out_uv    = v.uv;
    out_color = unpack_unorm4u8(v.color);
}
