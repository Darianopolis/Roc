#ifndef GPU_SHARED_H
#define GPU_SHARED_H

#ifdef __cplusplus
# include "gpu/gpu.hpp"
# define GPU_CONST_PTR(T) const T*
# define GPU_PTR(T) T*
# define GPU_PTR_ENABLE_FOR(T, Align) static_assert(alignof(T) == Align);
#else
# include "gpu/shaders/shared.glsl"
#endif // __cplusplus

GPU_PTR_ENABLE_FOR(u8,  1);
GPU_PTR_ENABLE_FOR(u16, 2);
GPU_PTR_ENABLE_FOR(u32, 4);
GPU_PTR_ENABLE_FOR(i32, 4);
GPU_PTR_ENABLE_FOR(f32, 4);

GPU_PTR_ENABLE_FOR(vec2f32, 4);
GPU_PTR_ENABLE_FOR(vec3f32, 4);
GPU_PTR_ENABLE_FOR(vec4f32, 4);
GPU_PTR_ENABLE_FOR(vec2i32, 4);
GPU_PTR_ENABLE_FOR(vec4u8,  1);
GPU_PTR_ENABLE_FOR(vec2u32, 4);

GPU_PTR_ENABLE_FOR(rect2f32, 4);
GPU_PTR_ENABLE_FOR(rect2i32, 4);

GPU_PTR_ENABLE_FOR(aabb2f32, 4);

GPU_PTR_ENABLE_FOR(GpuSampledImageHandle, 2);
GPU_PTR_ENABLE_FOR(GpuStorageImageHandle, 2);

#endif // GPU_SHARED_H
