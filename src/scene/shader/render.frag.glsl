#version 460
#extension GL_GOOGLE_include_directive : require

#include "render.h"

layout(push_constant, scalar) uniform PushConstants { SceneRenderInput si; };

layout(location = 0) in  vec2f32 in_uv;
layout(location = 1) in  vec4f32 in_color;
layout(location = 0) out vec4f32 out_color;

void main()
{
    out_color = image_sample(si.texture, in_uv) * in_color;
}
