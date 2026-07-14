#pragma once

#include "gpu.hpp"

#include <core/log.hpp>

// -----------------------------------------------------------------------------

auto gpu_check(VkResult res, auto... allowed) -> VkResult
{
    if (res == VK_SUCCESS || (... || (res == allowed))) return res;

    log_error("VULKAN ERROR: {}, ({})", enum_name(res), std::to_underlying(res));

    debug_kill();
}

template<typename Container, typename Fn, typename... Args>
void gpu_vulkan_enumerate(Container& container, Fn&& fn, Args&&... args)
{
    u32 count = static_cast<u32>(container.size());
    for (;;) {
        u32 old_count = count;
        if constexpr (std::same_as<VkResult, decltype(fn(args..., &count, nullptr))>) {
            gpu_check(fn(args..., &count, container.data()), VK_INCOMPLETE);
        } else {
            fn(args..., &count, container.data());
        }

        container.resize(count);
        if (count <= old_count) return;
    }
}

inline
auto gpu_vulkan_make_chain(std::span<void* const> structures) -> void*
{
    VkBaseInStructure* last = nullptr;
    for (auto* s : structures) {
        if (!s) continue;
        auto* vk_base = static_cast<VkBaseInStructure*>(s);
        vk_base->pNext = last;
        last = vk_base;
    }

    return last;
};

// -----------------------------------------------------------------------------

auto gpu_image_usage_to_vulkan(Flags<GpuImageUsage>) -> VkImageUsageFlags;
auto gpu_get_required_format_features(GpuFormat, Flags<GpuImageUsage>) -> VkFormatFeatureFlags;

auto gpu_find_memory_type_index(Gpu*, u32 type_filter, VkMemoryPropertyFlags required, VkMemoryPropertyFlags disallowed = {}) -> u32;

// -----------------------------------------------------------------------------

static constexpr u32 gpu_push_constant_size = 128;

void gpu_init_descriptors(Gpu*);
auto gpu_allocate_image_descriptor(Gpu*, VkImageView, VkDescriptorType) -> GpuDescriptorId;
auto gpu_allocate_sampler_descriptor(Gpu*, VkSampler) -> GpuDescriptorId;

// -----------------------------------------------------------------------------

struct GpuCommands
{
    Gpu* gpu;

    VkCommandBuffer buffer;
    RefVector<void> objects;

    u64 submitted_value;

#if GPU_VALIDATION_COMPATIBILITY
    struct {
        VkFence fence;
    } validation;
#endif

    ~GpuCommands();
};

void gpu_queue_init(Gpu*);
void gpu_protect(GpuCommands*, Ref<void>);

// -----------------------------------------------------------------------------

VkSemaphoreSubmitInfo gpu_syncpoint_to_submit_info(const GpuSyncpoint&);

struct GpuBinarySemaphore
{
    Gpu* gpu;

    VkSemaphore semaphore;

    ~GpuBinarySemaphore();
};

auto gpu_get_binary_semaphore(Gpu*) -> Ref<GpuBinarySemaphore>;
