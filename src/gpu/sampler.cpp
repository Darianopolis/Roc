#include "internal.hpp"

auto gpu_sampler_create(Gpu* gpu, const GpuSamplerCreateInfo& info) -> Ref<GpuSampler>
{
    Ref sampler = ref_create<GpuSampler>();
    sampler->gpu = gpu;

    gpu->stats.active_samplers++;

    gpu_check(gpu->vk.CreateSampler(gpu->device, ptr_to(VkSamplerCreateInfo {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = info.mag,
        .minFilter = info.min,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
        .anisotropyEnable = false,
        .maxLod = VK_LOD_CLAMP_NONE,
        .borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
    }), nullptr, &sampler->sampler));

    sampler->id = gpu_allocate_sampler_descriptor(gpu, sampler->sampler);

    return sampler;
}

GpuSampler::~GpuSampler()
{
    gpu->stats.active_samplers--;

    gpu->sampler_descriptor_allocator.free(id);

    gpu->vk.DestroySampler(gpu->device, sampler, nullptr);
}
