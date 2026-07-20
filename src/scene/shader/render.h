#ifndef SCENE_RENDER_H
#define SCENE_RENDER_H

#include "gpu/shaders/shared.h"

#define SCENE_RENDER_FLAG_PREMULTIPLIED (u32(1) << 0)
#define SCENE_RENDER_FLAG_OPAQUE        (u32(1) << 1)

struct SceneRenderQuad
{
    rect2f32 dst;

    GpuSampledImageHandle texture;
    rect2f32 src;

    vec4u8 tint;
    u32 flags;
};

GPU_PTR_ENABLE_FOR(SceneRenderQuad, 4);

// -----------------------------------------------------------------------------

#define SCENE_RENDER_QUAD_INDEX_TYPE u16
#define SCENE_RENDER_QUADS_PER_BIN 30

struct SceneRenderBin
{
    SCENE_RENDER_QUAD_INDEX_TYPE quads[SCENE_RENDER_QUADS_PER_BIN];
    u32 next_bin;
};

GPU_PTR_ENABLE_FOR(SceneRenderBin, 4);

// -----------------------------------------------------------------------------

#define SCENE_RENDER_COARSE_BIN_SIZE 128
#define SCENE_RENDER_FINE_BIN_SIZE 16
#define SCENE_RENDER_COARSE_FINE_BIN_RATIO (SCENE_RENDER_COARSE_BIN_SIZE / SCENE_RENDER_FINE_BIN_SIZE)

// The [0] bin is reserved as an invalid bin index
#define SCENE_RENDER_RESERVED_COARSE_BIN_COUNT 1

#define SCENE_RENDER_BIN_PASS_LOCAL_SIZE 8
#define SCENE_RENDER_PIXEL_PASS_LOCAL_SIZE 8

struct SceneRenderCoarseBinInfo
{
    u32 offset; // Index into fine bin array for output
    u32 depth;  // Maximum number of quads per fine bin
};

GPU_PTR_ENABLE_FOR(SceneRenderCoarseBinInfo, 4);

struct SceneRenderBinPassInput
{
    GPU_CONST_PTR(aabb2f32) quad_bounds;
    GPU_CONST_PTR(u8) quad_opaque_flags;
    GPU_CONST_PTR(SceneRenderBin) coarse_bins;
    GPU_CONST_PTR(SceneRenderCoarseBinInfo) coarse_bin_infos;
    GPU_PTR(u16) fine_bins;
    u32 coarse_bin_row_stride;
    vec2u32 extent;
};

struct SceneRenderPixelPassInput
{
    GPU_CONST_PTR(aabb2f32) quad_bounds;
    GPU_CONST_PTR(SceneRenderQuad) quads;
    GPU_CONST_PTR(SceneRenderCoarseBinInfo) coarse_bin_infos;
    GPU_CONST_PTR(u16) fine_bins;
    u32 coarse_bin_row_stride;
    GpuStorageImageHandle target;
    vec2u32 extent;
};

#endif // SCENE_RENDER_H
