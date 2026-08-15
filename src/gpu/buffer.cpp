#include "internal.hpp"

auto gpu_buffer_create(Gpu* gpu, usz size, Flags<GpuBufferFlag> flags) -> Ref<GpuBuffer>
{
    auto buffer = ref_create<GpuBuffer>();
    buffer->gpu = gpu;

    buffer->size = size;

    if (size == 0) return buffer;

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

    gpu_check(gpu->vk.AllocateMemory(gpu->device, ptr_to(VkMemoryAllocateInfo {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = ptr_to(VkMemoryAllocateFlagsInfo {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
            .flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT,
        }),
        .allocationSize = mem_reqs.size,
        .memoryTypeIndex = index.value(),
    }), nullptr, &buffer->memory));

    gpu_check(gpu->vk.MapMemory(gpu->device, buffer->memory, 0, size, {}, &buffer->host_address));

    gpu->vk.BindBufferMemory(gpu->device, buffer->buffer, buffer->memory, 0);

    buffer->device_address = gpu->vk.GetBufferDeviceAddress(gpu->device, ptr_to(VkBufferDeviceAddressInfo {
        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = buffer->buffer,
    }));

    return buffer;
}

GpuBuffer::~GpuBuffer()
{
    if (size == 0) return;

    gpu->stats.active_buffers--;

    VkMemoryRequirements mem_reqs;
    gpu->vk.GetBufferMemoryRequirements(gpu->device, buffer, &mem_reqs);
    gpu->vk.DestroyBuffer(gpu->device, buffer, nullptr);
    gpu->vk.FreeMemory(gpu->device, memory, nullptr);
}

// -----------------------------------------------------------------------------

auto gpu_reserve_transfer(GpuCommands* cmd, usz size, usz align) -> usz
{
    auto* gpu = cmd->gpu;
    auto& ring = gpu->transfer;
    auto* buf = ring.buffer.get();

    usz capacity = buf->size;

    usz aligned_head = align_up_power2(ring.head, align);
    usz reserved_end = aligned_head + size;
    usz wrap_point = align_up_power2(aligned_head, capacity);

    if (wrap_point > aligned_head && wrap_point < reserved_end) {
        aligned_head = wrap_point;
        reserved_end = aligned_head + size;
    }

    if (reserved_end - ring.tail > capacity) {
        usz new_capacity = round_up_power2(reserved_end - ring.tail);
        log_warn("Transfer ring buffer ran out of space ({}), allocating larger buffer ({})", FmtBytes{capacity}, FmtBytes{new_capacity});
        buf = (ring.buffer = gpu_buffer_create(gpu, new_capacity, GpuBufferFlag::host)).get();
        aligned_head = ring.head = ring.tail = 0;
        reserved_end = size;
        capacity = new_capacity;
    }

    debug_assert((num_cast<uintptr_t>(buf->host_address) & (align - 1)) == 0,
        "Reserve request has stricter alignment than supported by the transfer ring buffer");

    ring.head = reserved_end;

    cmd->new_transfer_tail = reserved_end;
    cmd->used_transfer_buffer = buf;

    return aligned_head & (capacity - 1);
}
