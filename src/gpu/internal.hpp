#pragma once

#include "gpu.hpp"

// -----------------------------------------------------------------------------

const char* gpu_result_to_string(VkResult res);

VkResult gpu_check(VkResult res, auto... allowed)
{
    if (res == VK_SUCCESS || (... || (res == allowed))) return res;

    log_error("VULKAN ERROR: {}, ({})", gpu_result_to_string(res), int(res));

    core_debugkill();
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
        auto vk_base = static_cast<VkBaseInStructure*>(s);
        vk_base->pNext = last;
        last = vk_base;
    }

    return last;
};

// -----------------------------------------------------------------------------

u32 gpu_find_vk_memory_type_index(gpu_context*, u32 type_filter, VkMemoryPropertyFlags properties);

VkFormatFeatureFlags gpu_get_required_format_features(gpu_format, flags<gpu_image_usage>);

// -----------------------------------------------------------------------------

struct gpu_image_base : gpu_image
{
    gpu_context* ctx;

    struct {
        gpu_format format;

        VkImage     image;
        VkImageView view;
        vec2u32     extent;

        gpu_descriptor_id id;

        flags<gpu_image_usage> usage;
    } base;

    virtual auto context()    const -> gpu_context*            final override { return ctx;         };
    virtual auto extent()     const -> vec2u32                 final override { return base.extent; };
    virtual auto format()     const -> gpu_format              final override { return base.format; };
    virtual auto view()       const -> VkImageView             final override { return base.view;   };
    virtual auto handle()     const -> VkImage                 final override { return base.image;  };
    virtual auto usage()      const -> flags<gpu_image_usage>  final override { return base.usage;  };
    virtual auto descriptor() const -> gpu_descriptor_id       final override { return base.id;     };

    virtual ~gpu_image_base();
};

void gpu_image_init(gpu_image_base*);

// -----------------------------------------------------------------------------

void gpu_init_descriptors(gpu_context*);
void gpu_allocate_image_descriptor(gpu_image_base*);
void gpu_allocate_sampler_descriptor(gpu_sampler*);

// -----------------------------------------------------------------------------

ref<gpu_queue> gpu_queue_init(gpu_context*, gpu_queue_type, u32 family);

// -----------------------------------------------------------------------------

VkSemaphoreSubmitInfo gpu_syncpoint_to_submit_info(const gpu_syncpoint& syncpoint);
