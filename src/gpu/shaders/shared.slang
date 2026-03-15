#pragma once

#define GPU_SHADER 1

// -----------------------------------------------------------------------------

__generic<T>
typealias gpu_const_ptr = Ptr<T, Access.Read>;

// -----------------------------------------------------------------------------

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t  i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

typedef float  f32;
typedef double f64;

// -----------------------------------------------------------------------------

typedef vector<f32, 2> vec2f32;
typedef vector<i32, 2> vec2i32;
typedef vector<u32, 2> vec2u32;

typedef vector<f32, 3> vec3f32;
typedef vector<i32, 3> vec3i32;
typedef vector<u32, 3> vec3u32;

typedef vector<u8,  4> vec4u8;
typedef vector<f32, 4> vec4f32;
typedef vector<i32, 4> vec4i32;
typedef vector<u32, 4> vec4u32;

// -----------------------------------------------------------------------------

typedef matrix<f32, 2, 2> mat2x2f32;
typedef matrix<f32, 2, 3> mat2x3f32;

typedef matrix<f32, 3, 2> mat3x2f32;
typedef matrix<f32, 3, 3> mat3x3f32;
typedef matrix<f32, 3, 4> mat3x4f32;

typedef matrix<f32, 4, 3> mat4x3f32;
typedef matrix<f32, 4, 4> mat4x4f32;

// -----------------------------------------------------------------------------

struct gpu_rect<T>
{
    vector<T, 2> origin;
    vector<T, 2> extent;
};

typedef gpu_rect<i32> rect2i32;
typedef gpu_rect<f32> rect2f32;

// -----------------------------------------------------------------------------

[[vk::binding(0, 0)]] __DynamicResource<__DynamicResourceKind.General> gpu_heap_texture[];
[[vk::binding(1, 0)]] __DynamicResource<__DynamicResourceKind.General> gpu_heap_storage[];
[[vk::binding(2, 0)]] __DynamicResource<__DynamicResourceKind.Sampler> gpu_heap_sampler[];

namespace gpu
{
    struct ImageHandle<T : ITexelElement>
    {
        u32 image   : 20;
        u32 sampler : 12;

        __init()
        {
            this.image = 0;
        }

        __init(u32 image)
        {
            this.image = image;
        }

        __init(u32 image, u32 sampler)
        {
            this.image = image;
            this.sampler = sampler;
        }

        bool valid()
        {
            return image != 0 && sampler != 0;
        }

        vec2u32 get_dimensions()
        {
            u32 width, height;
            gpu_heap_texture[NonUniformResourceIndex(image)].as<Texture2D<T>>().GetDimensions(width, height);

            return vec2u32(width, height);
        }

        T sample(vec2f32 uv)
        {
            return gpu_heap_texture[NonUniformResourceIndex(image)].as<Texture2D<T>>()
                .Sample(gpu_heap_sampler[NonUniformResourceIndex(sampler)].as<SamplerState>(), uv);
        }

        T sample_lod(vec2f32 uv, u32 lod)
        {
            return gpu_heap_texture[NonUniformResourceIndex(image)].as<Texture2D<T>>()
                .SampleLevel(gpu_heap_sampler[NonUniformResourceIndex(sampler)].as<SamplerState>(), uv, lod);
        }

        T load(vec2u32 idx)
        {
            return gpu_heap_texture[NonUniformResourceIndex(image)].as<Texture2D<T>>()[idx];
        }

        void store(vec2u32 idx, T value)
        {
            gpu_heap_storage[NonUniformResourceIndex(image)].as<RWTexture2D<T>>()
                .Store(idx, value);
        }
    };
}

typedef gpu::ImageHandle<vec4f32> image4f32;

// -----------------------------------------------------------------------------

vec4f32 core_unpack_unorm4u8(vec4u8 in)
{
    return vec4f32(in.x, in.y, in.z, in.w) / 255;
}
