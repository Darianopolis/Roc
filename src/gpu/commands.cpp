#include "internal.hpp"

#include "core/enum.hpp"
#include "core/stack.hpp"

core::Ref<gpu::Queue> gpu_queue_init(gpu::Context* gpu, gpu::QueueType type, u32 family)
{
    auto queue = core::create<gpu::Queue>();
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

    queue->queue_sema = gpu::semaphore::create(gpu);

    return queue;
}

gpu::Queue::~Queue()
{
    queue_sema = nullptr;

    gpu->vk.DestroyCommandPool(gpu->device, cmd_pool, nullptr);
}

gpu::Queue* gpu::queue::get(gpu::Context* gpu, gpu::QueueType type)
{
    switch (type) {
        break;case gpu::QueueType::graphics:
            return gpu->graphics_queue.get();
        break;case gpu::QueueType::transfer:
            return gpu->transfer_queue.get();
    }

    core::unreachable();
}

// -----------------------------------------------------------------------------

gpu::Commands::~Commands()
{
    auto* gpu = queue->gpu;
    gpu->vk.FreeCommandBuffers(gpu->device, queue->cmd_pool, 1, &buffer);
}

core::Ref<gpu::Commands> gpu::commands::begin(gpu::Queue* queue)
{
    auto* gpu = queue->gpu;
    auto commands = core::create<gpu::Commands>();
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

void gpu::commands::protect(gpu::Commands* commands, core::Ref<void> object)
{
    if (!object) return;
    commands->objects.emplace_back(std::move(object));
}

// -----------------------------------------------------------------------------

void gpu::wait_idle(gpu::Context* gpu)
{
    gpu::wait_idle(gpu->graphics_queue.get());
    gpu::wait_idle(gpu->transfer_queue.get());
}

void gpu::wait_idle(gpu::Queue* queue)
{
    queue->gpu->vk.QueueWaitIdle(queue->queue);
    gpu::wait({queue->queue_sema.get(), queue->submitted});
}

// -----------------------------------------------------------------------------

gpu::Syncpoint gpu::submit(gpu::Commands* commands, std::span<const gpu::Syncpoint> waits)
{
    auto* queue = commands->queue;
    auto* gpu = queue->gpu;

    core::ThreadStack stack;

    gpu_check(gpu->vk.EndCommandBuffer(commands->buffer));

    commands->submitted_value = ++queue->submitted;

    gpu::Syncpoint target_syncpoint {
        .semaphore = queue->queue_sema.get(),
        .value = commands->submitted_value,
    };

    auto* wait_infos = stack.allocate<VkSemaphoreSubmitInfo>(waits.size());
    for (auto[i, wait] : waits | std::views::enumerate) {
        wait_infos[i] = gpu_syncpoint_to_submit_info(wait);
        gpu::commands::protect(commands, wait.semaphore);
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

    gpu::wait({queue->queue_sema.get(), queue->submitted}, [commands = core::Ref(commands)](u64) {});

    return target_syncpoint;
}
