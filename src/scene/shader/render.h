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

GPU_PTR_ENABLE_FOR(SceneQuad, 4);

struct SceneRasterBlendPassInput
{
    GPU_CONST_PTR(SceneQuad) quads;
    vec2f32 offset;
    vec2f32 scale;
};

struct SceneRasterOutputPassInput
{
    GpuStorageImageHandle source;
    GpuStorageImageHandle target;
    vec2u32 extent;
};

#define SCENE_RASTER_OUTPUT_PASS_LOCAL_SIZE 8

// -----------------------------------------------------------------------------

#define SCENE_BIN_SIZE 16
#define SCENE_RESERVED_BIN_COUNT 1

#define SCENE_QUAD_INDEX_TYPE u16
#define SCENE_QUADS_PER_BIN 30

#define SCENE_COMPUTE_BIN_PASS_LOCAL_SIZE 8
#define SCENE_COMPUTE_PIXEL_PASS_LOCAL_SIZE 8

struct SceneComputeBin
{
    SCENE_QUAD_INDEX_TYPE quads[SCENE_QUADS_PER_BIN];
    u32 next_bin;
};

GPU_PTR_ENABLE_FOR(SceneComputeBin, 4);

struct SceneComputeBinPassInput
{
    GPU_CONST_PTR(aabb2f32) quad_bounds;
    GPU_CONST_PTR(u8) quad_opaque_flags;
    u32 quad_count;
    GPU_PTR(SceneComputeBin) bins;
    u32 bin_count;
    u32 row_stride;
    vec2u32 extent;
};

struct SceneComputePixelPassInput
{
    GPU_CONST_PTR(aabb2f32) quad_bounds;
    GPU_CONST_PTR(SceneQuad) quads;
    u32 quad_count;
    GPU_CONST_PTR(SceneComputeBin) bins;
    u32 row_stride;
    GpuStorageImageHandle target;
    vec2u32 extent;
};

#endif // SCENE_RENDER_H
