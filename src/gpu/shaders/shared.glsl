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

#define vec2f32   vec2
#define vec3f32   vec3
#define vec4f32   vec4
#define vec2i32  ivec2
#define vec4u8  u8vec4
#define vec2u32  uvec2

struct rect2f32 { vec2f32 origin; vec2f32 extent; };
struct rect2i32 { vec2i32 origin; vec2i32 extent; };

struct aabb2f32 { vec2f32 min; vec2f32 max; };

// -----------------------------------------------------------------------------

#define GPU_CONST_PTR(T) T##_ConstPtr
#define GPU_PTR(T)       T##_Ptr

#define GPU_PTR_ENABLE_FOR(T) \
    layout(buffer_reference, scalar) readonly buffer T##_ConstPtr { T data[]; }; \
    layout(buffer_reference, scalar)          buffer T##_Ptr      { T data[]; };

// -----------------------------------------------------------------------------

layout(set=0, binding=0) uniform texture2D gpu_heap_texture[];
layout(set=0, binding=1) uniform image2D   gpu_heap_storage[];
layout(set=0, binding=2) uniform sampler   gpu_heap_sampler[];

// -----------------------------------------------------------------------------

struct GpuSampledImageHandle { u16 img; u16 smp; };

bool gpu_image_valid(GpuSampledImageHandle h)
{
    return h.img != 0 && h.smp != 0;
}

vec2i32 gpu_image_dimensions(GpuSampledImageHandle h)
{
    return vec2i32(textureSize(sampler2D(
        gpu_heap_texture[nonuniformEXT(u32(h.img))],
        gpu_heap_sampler[nonuniformEXT(u32(h.smp))]), 0));
}

vec4f32 gpu_image_sample(GpuSampledImageHandle h, vec2f32 uv)
{
    return texture(sampler2D(
        gpu_heap_texture[nonuniformEXT(u32(h.img))],
        gpu_heap_sampler[nonuniformEXT(u32(h.smp))]), uv);
}

vec4f32 gpu_image_sample_lod(GpuSampledImageHandle h, vec2f32 uv, i32 lod)
{
    return textureLod(sampler2D(
        gpu_heap_texture[nonuniformEXT(u32(h.img))],
        gpu_heap_sampler[nonuniformEXT(u32(h.smp))]), uv, f32(lod));
}

// -----------------------------------------------------------------------------

struct GpuStorageImageHandle { u16 img; };

bool gpu_image_valid(GpuStorageImageHandle h)
{
    return h.img != 0;
}

vec2i32 gpu_image_dimensions(GpuStorageImageHandle h)
{
    return vec2i32(imageSize(gpu_heap_storage[nonuniformEXT(u32(h.img))]));
}

vec4f32 gpu_image_load(GpuStorageImageHandle h, vec2i32 idx)
{
    return imageLoad(gpu_heap_storage[nonuniformEXT(u32(h.img))], idx);
}

void gpu_image_store(GpuStorageImageHandle h, vec2i32 idx, vec4f32 color)
{
    imageStore(gpu_heap_storage[nonuniformEXT(u32(h.img))], idx, color);
}

// -----------------------------------------------------------------------------

vec4f32 unpack_unorm4u8(vec4u8 v)
{
    return vec4f32(v) / 255.0;
}

// -----------------------------------------------------------------------------

vec3f32 srgb_oetf(vec3 linear)
{
    bvec3 cutoff = lessThan(linear, vec3(0.0031308));
    vec3 higher = vec3(1.055) * pow(linear, vec3(1.0 / 2.4)) - vec3(0.055);
    vec3 lower = linear * vec3(12.92);

    return mix(higher, lower, cutoff);
}

vec3f32 srgb_eotf(vec3 compressed)
{
    bvec3 cutoff = lessThan(compressed, vec3(0.04045));
    vec3 higher = pow((compressed + vec3(0.055)) / vec3(1.055), vec3(2.4));
    vec3 lower = compressed / vec3(12.92);

    return mix(higher, lower, cutoff);
}

// -----------------------------------------------------------------------------

#endif // GPU_SHARED_GLSL
