#include "internal.hpp"

#include <core/enum.hpp>
#include <core/stack.hpp>

void gpu_queue_init(Gpu* gpu)
{
    gpu->vk.GetDeviceQueue(gpu->device, gpu->queue.family, 0, &gpu->queue.queue);

    gpu_check(gpu->vk.CreateCommandPool(gpu->device, ptr_to(VkCommandPoolCreateInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
        .queueFamilyIndex = gpu->queue.family,
    }), nullptr, &gpu->queue.pool));

    gpu->queue.syncobj = gpu_syncobj_create(gpu);
}

// -----------------------------------------------------------------------------

GpuCommands::~GpuCommands()
{
    gpu->vk.FreeCommandBuffers(gpu->device, gpu->queue.pool, 1, &buffer);

#if GPU_VALIDATION_COMPATIBILITY
    if (validation.fence) {
        gpu->vk.DestroyFence(gpu->device, validation.fence, nullptr);
    }
#endif
}

auto gpu_get_commands(Gpu* gpu) -> GpuCommands*
{
    if (gpu->queue.commands) return gpu->queue.commands.get();

    auto commands = ref_create<GpuCommands>();
    commands->gpu = gpu;

    gpu_check(gpu->vk.AllocateCommandBuffers(gpu->device, ptr_to(VkCommandBufferAllocateInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = gpu->queue.pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    }), &commands->buffer));

    gpu_check(gpu->vk.BeginCommandBuffer(commands->buffer, ptr_to(VkCommandBufferBeginInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    })));

    gpu->queue.commands = std::move(commands);

    return gpu->queue.commands.get();
}

// -----------------------------------------------------------------------------

void gpu_protect(Gpu* gpu, Ref<void> object)
{
    if (!object) return;
    gpu_get_commands(gpu)->objects.emplace_back(std::move(object));
}

// -----------------------------------------------------------------------------

static
void transfer(GpuBinarySemaphore* from, GpuSyncobj* to, u64 to_point)
{
    auto gpu = from->gpu;

    int syncobj_fd = -1;
    gpu_check(gpu->vk.GetSemaphoreFdKHR(gpu->device, ptr_to(VkSemaphoreGetFdInfoKHR {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR,
        .semaphore = from->semaphore,
        .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT,
    }), &syncobj_fd));

    gpu_syncobj_import_syncfile(to, to_point, syncobj_fd);
    close(syncobj_fd);
}

static
auto submit_info(GpuBinarySemaphore* semaphore, VkPipelineStageFlags2 stages) -> VkSemaphoreSubmitInfo
{
    return {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = semaphore->semaphore,
        .stageMask = stages,
    };
}

auto gpu_flush(Gpu* gpu) -> GpuSyncpoint
{
    auto* commands = gpu->queue.commands.get();

    gpu_check(gpu->vk.EndCommandBuffer(commands->buffer));

    commands->submitted_value = ++gpu->queue.submitted;

    GpuSyncpoint target {
        .syncobj = gpu->queue.syncobj.get(),
        .value = commands->submitted_value,
    };

    // Signal does not require protection, as exporting resets its payload.
    auto signal = gpu->features.contains(GpuFeature::timelines)
        ? Ref<GpuBinarySemaphore>{}
        : gpu_get_binary_semaphore(gpu);

#if GPU_VALIDATION_COMPATIBILITY
    if (signal && gpu->features.contains(GpuFeature::validation)) {
        // When validation layers are enabled, they need visibility of command completion
        // otherwise they will complain about resources still being used.
        gpu_check(gpu->vk.CreateFence(gpu->device, ptr_to(VkFenceCreateInfo {
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        }), nullptr, &commands->validation.fence));
    }
#endif

    gpu_check(gpu->vk.QueueSubmit2(gpu->queue.queue, 1,
        ptr_to(VkSubmitInfo2 {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
            .commandBufferInfoCount = 1,
            .pCommandBufferInfos = ptr_to(VkCommandBufferSubmitInfo {
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
                .commandBuffer = commands->buffer,
            }),
            .signalSemaphoreInfoCount = 1,
            .pSignalSemaphoreInfos = signal
                ? ptr_to(submit_info(signal.get(), VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT))
                : ptr_to(gpu_syncpoint_to_submit_info(target)),
        }),
#if GPU_VALIDATION_COMPATIBILITY
        commands->validation.fence
#else
        nullptr
#endif
        ));

    if (signal) {
        transfer(signal.get(), target.syncobj, target.value);
    }

    gpu_wait({gpu->queue.syncobj.get(), gpu->queue.submitted}, [commands = Ref(commands)](u64) {
#if GPU_VALIDATION_COMPATIBILITY
        if (commands->validation.fence) {
            auto* gpu = commands->gpu;
            gpu_check(gpu->vk.WaitForFences(gpu->device, 1, &commands->validation.fence, true, UINT64_MAX));
        }
#endif
    });

    gpu->queue.commands = nullptr;

    return target;
}
