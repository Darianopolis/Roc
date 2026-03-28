#ifndef GPU_SHARED_GLSL
#define GPU_SHARED_GLSL

#extension GL_EXT_nonuniform_qualifier             : require
#extension GL_EXT_scalar_block_layout              : require
#extension GL_EXT_buffer_reference2                : require
#extension GL_EXT_shader_explicit_arithmetic_types : require
#extension GL_EXT_shader_image_load_formatted      : require

// -----------------------------------------------------------------------------

#define u8  uint8_t
#define u16 uint16_t
#define u32 uint
#define i32 int
#define f32 float
#define vec2f32 vec2
#define vec3f32 vec3
#define vec4f32 vec4
#define vec2i32 ivec2
#define vec4u8  u8vec4
#define vec2u32 uvec2

struct rect2f32 { vec2f32 origin; vec2f32 extent; };
struct rect2i32 { vec2i32 origin; vec2i32 extent; };

// -----------------------------------------------------------------------------

layout(set=0, binding=0) uniform texture2D gpu_heap_texture[];
layout(set=0, binding=1) uniform image2D   gpu_heap_storage[];
layout(set=0, binding=2) uniform sampler   gpu_heap_sampler[];

// -----------------------------------------------------------------------------

struct GpuImageHandle { u16 img; u16 smp; };

// -----------------------------------------------------------------------------

#define GPU_CONST_PTR(T) T##_Ptr
#define GPU_CONST_PTR_DECLARE(T) \
    layout(buffer_reference, scalar) readonly buffer GPU_CONST_PTR(T) { T data[]; }

// -----------------------------------------------------------------------------

bool image_valid(GpuImageHandle h) { return h.img != 0 && h.smp != 0; }

vec2i32 image_dimensions(GpuImageHandle h)
{
    return vec2i32(textureSize(sampler2D(
        gpu_heap_texture[nonuniformEXT(u32(h.img))],
        gpu_heap_sampler[nonuniformEXT(u32(h.smp))]), 0));
}

vec4f32 image_sample(GpuImageHandle h, vec2f32 uv)
{
    return texture(sampler2D(
        gpu_heap_texture[nonuniformEXT(u32(h.img))],
        gpu_heap_sampler[nonuniformEXT(u32(h.smp))]), uv);
}

vec4f32 image_sample_lod(GpuImageHandle h, vec2f32 uv, i32 lod)
{
    return textureLod(sampler2D(
        gpu_heap_texture[nonuniformEXT(u32(h.img))],
        gpu_heap_sampler[nonuniformEXT(u32(h.smp))]), uv, f32(lod));
}

vec4f32 image_load(GpuImageHandle h, vec2i32 idx)
{
    return imageLoad(gpu_heap_storage[nonuniformEXT(u32(h.img))], idx);
}

// -----------------------------------------------------------------------------

vec4f32 unpack_unorm4u8(vec4u8 v) { return vec4f32(v) / 255.0; }

// -----------------------------------------------------------------------------

#endif // GPU_SHARED_GLSL
