#pragma once

#include "gpu.hpp"

// -----------------------------------------------------------------------------

auto gpu_result_to_string(VkResult res) -> const char*;

auto gpu_check(VkResult res, auto... allowed) -> VkResult
{
    if (res == VK_SUCCESS || (... || (res == allowed))) return res;

    log_error("VULKAN ERROR: {}, ({})", gpu_result_to_string(res), int(res));

    debug_kill();
}

template<typename Container, typename Fn, typename... Args>
void gpu_vk_enumerate(Container& container, Fn&& fn, Args&&... args)
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
auto gpu_vk_make_chain_in(std::span<void* const> structures)
{
    VkBaseInStructure* last = nullptr;
    for (auto* s : structures) {
        auto* vk_base = static_cast<VkBaseInStructure*>(s);
        vk_base->pNext = last;
        last = vk_base;
    }

    return last;
};

// -----------------------------------------------------------------------------

auto gpu_find_vk_memory_type_index(Gpu*, u32 type_filter, VkMemoryPropertyFlags properties) -> u32;

auto gpu_get_required_format_features(GpuFormat, Flags<GpuImageUsage>) -> VkFormatFeatureFlags;

// -----------------------------------------------------------------------------

auto gpu_image_usage_to_vk(Flags<GpuImageUsage>) -> VkImageUsageFlags;

struct GpuImageBase : GpuImage
{
    Gpu* gpu;

    struct {
        GpuFormat format;
        GpuDrmModifier modifier = DRM_FORMAT_MOD_INVALID;

        VkImage     image;
        VkImageView view;
        vec2u32     extent;

        GpuDescriptorId id;

        Flags<GpuImageUsage> usage;
    } data;

    virtual ~GpuImageBase();

    virtual auto base() -> GpuImage* final override { return this; }
};

void gpu_image_init(GpuImageBase*);

auto gpu_image_create_dmabuf(Gpu*, const GpuImageCreateInfo&) -> Ref<GpuImage>;

// -----------------------------------------------------------------------------

static constexpr u32 gpu_push_constant_size = 128;

void gpu_init_descriptors(Gpu*);
void gpu_allocate_image_descriptor(GpuImageBase*);
void gpu_allocate_sampler_descriptor(GpuSampler*);

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

void gpu_queue_init(  Gpu*);
auto gpu_get_commands(Gpu*) -> GpuCommands*;

// -----------------------------------------------------------------------------

VkSemaphoreSubmitInfo gpu_syncpoint_to_submit_info(const GpuSyncpoint&);

struct GpuBinarySemaphore
{
    Gpu* gpu;

    VkSemaphore semaphore;

    ~GpuBinarySemaphore();
};

auto gpu_get_binary_semaphore(Gpu*) -> Ref<GpuBinarySemaphore>;
