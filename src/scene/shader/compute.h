#ifndef SCENE_COMPUTE_H
#define SCENE_COMPUTE_H

#include "render.h"

struct SceneComputeInput
{
    GPU_CONST_PTR(SceneQuad) quad_stack;
    u32                      quad_count;
    rect2i32 region;
    GpuImageHandle target;
    vec4f32 debug_color;
};

#endif // SCENE_COMPUTE_H
