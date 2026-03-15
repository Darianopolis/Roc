#include "internal.hpp"

core::Ref<gpu::Buffer> gpu::buffer::create(gpu::Context* gpu, usz size, core::Flags<gpu::BufferFlag> flags)
{
    auto buffer = core::create<gpu::Buffer>();
    buffer->gpu = gpu;

    buffer->size = size;

    gpu->stats.active_buffers++;

    VmaAllocationInfo alloc_info;
    gpu_check(vmaCreateBuffer(gpu->vma,
        core::ptr_to(VkBufferCreateInfo {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = size,
            .usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
                   | VK_BUFFER_USAGE_TRANSFER_SRC_BIT
                   | VK_BUFFER_USAGE_TRANSFER_DST_BIT
                   | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            .sharingMode = VK_SHARING_MODE_CONCURRENT,
            .queueFamilyIndexCount = 2,
            .pQueueFamilyIndices = std::array {
                gpu->graphics_queue->family,
                gpu->transfer_queue->family,
            }.data(),
        }),
        flags.contains(gpu::BufferFlag::host)
            ? core::ptr_to(VmaAllocationCreateInfo {
                .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
                .usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
                .requiredFlags = VK_MEMORY_PROPERTY_HOST_CACHED_BIT
            })
            : core::ptr_to(VmaAllocationCreateInfo {
                .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
                .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
            }),
        &buffer->buffer, &buffer->vma_allocation, &alloc_info));

    gpu->stats.active_buffer_memory += alloc_info.size;

    buffer->host_address = alloc_info.pMappedData;

    buffer->device_address = gpu->vk.GetBufferDeviceAddress(gpu->device, core::ptr_to(VkBufferDeviceAddressInfo {
        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = buffer->buffer,
    }));

    return buffer;
}

gpu::Buffer::~Buffer()
{
    gpu->stats.active_buffers--;

    VmaAllocationInfo alloc_info;
    vmaGetAllocationInfo(gpu->vma, vma_allocation, &alloc_info);
    gpu->stats.active_buffer_memory -= alloc_info.size;

    vmaDestroyBuffer(gpu->vma, buffer, vma_allocation);
}
