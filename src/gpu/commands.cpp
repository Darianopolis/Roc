#include "internal.hpp"

#include "core/enum.hpp"
#include "core/stack.hpp"

core::Ref<gpu_queue> gpu_queue_init(gpu_context* gpu, gpu_queue_type type, u32 family)
{
    auto queue = core::create<gpu_queue>();
    queue->gpu = gpu;
    queue->type = type;
    queue->family = family;

    log_debug("Queue created of type \"{}\" with family {}", core::to_string(type), family);

    gpu->vk.GetDeviceQueue(gpu->device, family, 0, &queue->queue);

    gpu_check(gpu->vk.CreateCommandPool(gpu->device, core::ptr_to(VkCommandPoolCreateInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
        .queueFamilyIndex = queue->family,
    }), nullptr, &queue->cmd_pool));

    queue->queue_sema = gpu_semaphore_create(gpu);

    return queue;
}

gpu_queue::~gpu_queue()
{
    queue_sema = nullptr;

    gpu->vk.DestroyCommandPool(gpu->device, cmd_pool, nullptr);
}

gpu_queue* gpu_get_queue(gpu_context* gpu, gpu_queue_type type)
{
    switch (type) {
        break;case gpu_queue_type::graphics:
            return gpu->graphics_queue.get();
        break;case gpu_queue_type::transfer:
            return gpu->transfer_queue.get();
    }

    core::unreachable();
}

// -----------------------------------------------------------------------------

gpu_commands::~gpu_commands()
{
    auto* gpu = queue->gpu;
    gpu->vk.FreeCommandBuffers(gpu->device, queue->cmd_pool, 1, &buffer);
}

core::Ref<gpu_commands> gpu_commands_begin(gpu_queue* queue)
{
    auto* gpu = queue->gpu;
    auto commands = core::create<gpu_commands>();
    commands->queue = queue;

    gpu_check(gpu->vk.AllocateCommandBuffers(gpu->device, core::ptr_to(VkCommandBufferAllocateInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = queue->cmd_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    }), &commands->buffer));

    gpu_check(gpu->vk.BeginCommandBuffer(commands->buffer, core::ptr_to(VkCommandBufferBeginInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    })));

    return commands;
}

// -----------------------------------------------------------------------------

void gpu_cmd_protect(gpu_commands* commands, core::Ref<void> object)
{
    if (!object) return;
    commands->objects.emplace_back(std::move(object));
}

// -----------------------------------------------------------------------------

void gpu_wait_idle(gpu_context* gpu)
{
    gpu_wait_idle(gpu->graphics_queue.get());
    gpu_wait_idle(gpu->transfer_queue.get());
}

void gpu_wait_idle(gpu_queue* queue)
{
    queue->gpu->vk.QueueWaitIdle(queue->queue);
    gpu_wait({queue->queue_sema.get(), queue->submitted});
}

// -----------------------------------------------------------------------------

gpu_syncpoint gpu_submit(gpu_commands* commands, std::span<const gpu_syncpoint> waits)
{
    auto* queue = commands->queue;
    auto* gpu = queue->gpu;

    core::ThreadStack stack;

    gpu_check(gpu->vk.EndCommandBuffer(commands->buffer));

    commands->submitted_value = ++queue->submitted;

    gpu_syncpoint target_syncpoint {
        .semaphore = queue->queue_sema.get(),
        .value = commands->submitted_value,
    };

    auto* wait_infos = stack.allocate<VkSemaphoreSubmitInfo>(waits.size());
    for (auto[i, wait] : waits | std::views::enumerate) {
        wait_infos[i] = gpu_syncpoint_to_submit_info(wait);
        gpu_cmd_protect(commands, wait.semaphore);
    }

    gpu_check(gpu->vk.QueueSubmit2(queue->queue, 1, core::ptr_to(VkSubmitInfo2 {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .waitSemaphoreInfoCount = u32(waits.size()),
        .pWaitSemaphoreInfos = wait_infos,
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = core::ptr_to(VkCommandBufferSubmitInfo {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
            .commandBuffer = commands->buffer,
        }),
        .signalSemaphoreInfoCount = 1,
        .pSignalSemaphoreInfos = core::ptr_to(gpu_syncpoint_to_submit_info(target_syncpoint)),
    }), nullptr));

    gpu_wait({queue->queue_sema.get(), queue->submitted}, [commands = core::Ref(commands)](u64) {});

    return target_syncpoint;
}
