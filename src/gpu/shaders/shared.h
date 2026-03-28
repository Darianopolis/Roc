#ifndef GPU_SHARED_H
#define GPU_SHARED_H

#ifdef __cplusplus
# include "gpu/gpu.hpp"
# define GPU_CONST_PTR(T) const T*
# define GPU_CONST_PTR_DECLARE(T)
#else
# include "gpu/shaders/shared.glsl"
#endif // __cplusplus

#endif // GPU_SHARED_H
