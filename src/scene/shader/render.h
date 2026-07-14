#ifndef SCENE_RENDER_H
#define SCENE_RENDER_H

#include "gpu/shaders/shared.h"

#define SCENE_DRAW_FLAG_PREMULTIPLIED (u32(1) << 0)

struct SceneQuad
{
    rect2f32 dst;

    GpuSampledImageHandle texture;
    rect2f32 src;

    vec4u8 tint;
    u32 flags;
};

GPU_CONST_PTR_DECLARE(SceneQuad);

struct SceneRenderInput
{
    GPU_CONST_PTR(SceneQuad) quads;
    vec2f32 offset;
    vec2f32 scale;
};

#endif // SCENE_RENDER_H
