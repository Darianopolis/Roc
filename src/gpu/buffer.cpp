#include "internal.hpp"

auto gpu_buffer_create(Gpu* gpu, usz size, Flags<GpuBufferFlag> flags) -> Ref<GpuBuffer>
{
    auto buffer = ref_create<GpuBuffer>();
    buffer->gpu = gpu;

    buffer->size = size;

    gpu->stats.active_buffers++;

    VmaAllocationInfo alloc_info;
    gpu_check(vmaCreateBuffer(gpu->vma,
        ptr_to(VkBufferCreateInfo {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = size,
            .usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
                   | VK_BUFFER_USAGE_TRANSFER_SRC_BIT
                   | VK_BUFFER_USAGE_TRANSFER_DST_BIT
                   | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        }),
        flags.contains(GpuBufferFlag::host)
            ? ptr_to(VmaAllocationCreateInfo {
                .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
                .usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
                .requiredFlags = VK_MEMORY_PROPERTY_HOST_CACHED_BIT
            })
            : ptr_to(VmaAllocationCreateInfo {
                .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
                .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
            }),
        &buffer->buffer, &buffer->vma_allocation, &alloc_info));

    gpu->stats.active_buffer_memory += alloc_info.size;

    buffer->host_address = alloc_info.pMappedData;

    buffer->device_address = gpu->vk.GetBufferDeviceAddress(gpu->device, ptr_to(VkBufferDeviceAddressInfo {
        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = buffer->buffer,
    }));

    return buffer;
}

GpuBuffer::~GpuBuffer()
{
    gpu->stats.active_buffers--;

    VmaAllocationInfo alloc_info;
    vmaGetAllocationInfo(gpu->vma, vma_allocation, &alloc_info);
    gpu->stats.active_buffer_memory -= alloc_info.size;

    vmaDestroyBuffer(gpu->vma, buffer, vma_allocation);
}
