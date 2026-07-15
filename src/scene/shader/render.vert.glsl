#version 460
#extension GL_GOOGLE_include_directive : require

#include "render.h"

layout(push_constant, scalar) uniform PushConstants { SceneRenderInput pc; };

layout(location = 0)      out vec2f32 out_uv;
layout(location = 1) flat out     u32 out_quad;

const vec2f32 positions[] = {
    vec2f32(0, 0), vec2f32(1, 0),
    vec2f32(0, 1), vec2f32(1, 1),
};

void main()
{
    GPU_CONST_PTR(SceneQuad) quad = pc.quads[gl_InstanceIndex];

    vec2f32 local_pos = positions[gl_VertexIndex];
    if ((quad._.flags & SCENE_DRAW_FLAG_OPAQUE) == 0) {
        // Flip non-opaque geometry winding order so that they don't update the stencil mask
        local_pos.x = 1 - local_pos.x;
    }

    vec2f32 pixel_pos = fma(local_pos, quad._.dst.extent, quad._.dst.origin);
    gl_Position = vec4f32(fma(pixel_pos, pc.scale, pc.offset), 0, 1);

    out_uv = fma(local_pos, quad._.src.extent, quad._.src.origin);
    out_quad = gl_InstanceIndex;
}
