#version 460
#extension GL_GOOGLE_include_directive : require

#include "render.h"

layout(push_constant, scalar) uniform PushConstants { SceneRenderInput si; };

layout(location = 0) in vec2f32 in_uv;
layout(location = 1) in vec4f32 in_color;

layout(location = 0) out vec4f32 out_color;

f32 sdf_rounded_box(vec2f32 p, vec2f32 b, vec4f32 r)
{
    r.xy = (p.x > 0.0) ? r.xy : r.zw;
    r.x  = (p.y > 0.0) ? r.x  : r.y;
    vec2f32 q = abs(p) - b + r.x;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r.x;
}

void main()
{
    f32 s = sdf_rounded_box(gl_FragCoord.xy - si.clip.origin, si.clip.extent, si.radius);
    f32 coverage = clamp(0.5 - s, 0, 1) * si.opacity;
    if (coverage == 0) discard;

    vec4f32 color = image_sample(si.texture, in_uv) * in_color;
    if ((si.flags & SCENE_DRAW_FLAG_PREMULTIPLIED) == 0) {
        color.rgb *= color.a;
    }
    out_color = color * coverage;
}
