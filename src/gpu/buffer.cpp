#include "internal.hpp"

auto gpu_buffer_create(Gpu* gpu, usz size, Flags<GpuBufferFlag> flags) -> Ref<GpuBuffer>
{
    auto buffer = ref_create<GpuBuffer>();
    buffer->gpu = gpu;

    buffer->size = size;

    gpu->stats.active_buffers++;

    gpu_check(gpu->vk.CreateBuffer(gpu->device, ptr_to(VkBufferCreateInfo {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
               | VK_BUFFER_USAGE_TRANSFER_SRC_BIT
               | VK_BUFFER_USAGE_TRANSFER_DST_BIT
               | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    }), nullptr, &buffer->buffer));

    // Allocate memory

    VkMemoryRequirements mem_reqs;
    gpu->vk.GetBufferMemoryRequirements(gpu->device, buffer->buffer, &mem_reqs);

    auto index = flags.contains(GpuBufferFlag::host)
        ? gpu_find_memory_type_index(gpu, mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_CACHED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
        : gpu_find_memory_type_index(gpu, mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    auto cache_size = round_up_power2(mem_reqs.size);
    auto& cache = gpu->buffer_allocation_cache[cache_size];
    if (cache.empty()) {
        gpu_check(gpu->vk.AllocateMemory(gpu->device, ptr_to(VkMemoryAllocateInfo {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .pNext = ptr_to(VkMemoryAllocateFlagsInfo {
                .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
                .flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT,
            }),
            .allocationSize = cache_size,
            .memoryTypeIndex = index.value(),
        }), nullptr, &buffer->memory));
    } else {
        buffer->memory = cache.back();
        cache.pop_back();
    }

    gpu->vk.BindBufferMemory(gpu->device, buffer->buffer, buffer->memory, 0);

    gpu_check(gpu->vk.MapMemory(gpu->device, buffer->memory, 0, size, {}, &buffer->host_address));

    buffer->device_address = gpu->vk.GetBufferDeviceAddress(gpu->device, ptr_to(VkBufferDeviceAddressInfo {
        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = buffer->buffer,
    }));

    return buffer;
}

GpuBuffer::~GpuBuffer()
{
    gpu->stats.active_buffers--;

    VkMemoryRequirements mem_reqs;
    gpu->vk.GetBufferMemoryRequirements(gpu->device, buffer, &mem_reqs);
    auto cache_size = round_up_power2(mem_reqs.size);
    gpu->buffer_allocation_cache[cache_size].emplace_back(memory);
    gpu->vk.DestroyBuffer(gpu->device, buffer, nullptr);
}
