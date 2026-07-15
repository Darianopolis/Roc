#ifndef SCENE_RENDER_H
#define SCENE_RENDER_H

#include "gpu/shaders/shared.h"

#define SCENE_DRAW_FLAG_PREMULTIPLIED (u32(1) << 0)
#define SCENE_DRAW_FLAG_OPAQUE        (u32(1) << 1)

struct SceneQuad
{
    rect2f32 dst;

    GpuSampledImageHandle texture;
    rect2f32 src;

    vec4u8 tint;
    u32 flags;
};

GPU_PTR_ENABLE_FOR(SceneQuad);

struct SceneRenderInput
{
    GPU_CONST_PTR(SceneQuad) quads;
    vec2f32 offset;
    vec2f32 scale;
};

// -----------------------------------------------------------------------------

#define SCENE_BIN_SIZE 16

#define SCENE_QUAD_INDEX_TYPE u16
#define SCENE_QUADS_PER_BIN 30

#define SCENE_BIN_DISPATCH_SIZE 8
#define SCENE_COMPUTE_DISPATCH_SIZE 8

struct SceneRenderBin
{
    SCENE_QUAD_INDEX_TYPE quads[SCENE_QUADS_PER_BIN];
    u32 next_bin;
};

GPU_PTR_ENABLE_FOR(SceneRenderBin);
GPU_PTR_ENABLE_FOR(aabb2f32);
GPU_PTR_ENABLE_FOR(u8);

struct SceneRenderBinInput
{
    GPU_CONST_PTR(aabb2f32) quad_bounds;
    GPU_CONST_PTR(u8) quad_opaque_flags;
    u32 quad_count;
    GPU_PTR(SceneRenderBin) bins;
    u32 bin_count;
    u32 row_stride;
    vec2u32 extent;
};

struct SceneRenderComputeInput
{
    GPU_CONST_PTR(aabb2f32) quad_bounds;
    GPU_CONST_PTR(SceneQuad) quads;
    u32 quad_count;
    GPU_CONST_PTR(SceneRenderBin) bins;
    u32 row_stride;
    GpuStorageImageHandle target;
    vec2u32 extent;
};

#endif // SCENE_RENDER_H
