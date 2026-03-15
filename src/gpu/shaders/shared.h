#pragma once

#ifdef __cplusplus

#include "gpu/gpu.hpp"

using image4f32 = gpu::ImageHandle<vec4f32>;

template<typename T>
using gpu_const_ptr = const T*;

#else
#include "shared.slang"
#endif
