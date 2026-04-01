#ifndef SCENE_RENDER_H
#define SCENE_RENDER_H

#include "gpu/shaders/shared.h"

struct SceneVertex
{
    vec2f32 pos;
    vec2f32 uv;
    vec4u8 color;
};

GPU_CONST_PTR_DECLARE(SceneVertex);

struct SceneRenderInput
{
    GPU_CONST_PTR(SceneVertex) vertices;
    vec2f32 scale;
    vec2f32 offset;
    GpuImageHandle texture;
    rect2f32 clip;
    vec4f32 radius;
};

#endif // SCENE_RENDER_H
