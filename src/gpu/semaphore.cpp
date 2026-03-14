#include "internal.hpp"

VkSemaphoreSubmitInfo gpu_syncpoint_to_submit_info(const gpu_syncpoint& syncpoint)
{
    return {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = syncpoint.semaphore->semaphore,
        .value     = syncpoint.value,
        .stageMask = syncpoint.stages,
    };
}

static
ref<gpu_semaphore> create_semaphore_base(gpu_context* gpu)
{
    auto semaphore = core_create<gpu_semaphore>();
    semaphore->gpu = gpu;

    gpu_check(gpu->vk.CreateSemaphore(gpu->device, ptr_to(VkSemaphoreCreateInfo {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = ptr_to(VkSemaphoreTypeCreateInfo {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
            .pNext = ptr_to(VkExportSemaphoreCreateInfo {
                .sType = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO,
                .handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT,
            }),
            .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
        })
    }), nullptr, &semaphore->semaphore));

    return semaphore;
}

ref<gpu_semaphore> gpu_semaphore_create(gpu_context* gpu)
{
    // Here we are creating a timeline sempahore, and exporting a persistent syncobj
    // handle to it that we can use for importing/exporting syncobj files for interop.
    // This trick only works when the driver uses syncobjs as the opaque fd type.
    // This seems to work fine on AMD, but definitely won't work for all vendors.

    // As a portable solution, we'll have to use DRM syncobjs as the underlying type, creating
    // binary semaphores as required for vulkan waits

    auto semaphore = create_semaphore_base(gpu);

    int syncobj_fd = -1;
    gpu_check(gpu->vk.GetSemaphoreFdKHR(gpu->device, ptr_to(VkSemaphoreGetFdInfoKHR {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR,
        .semaphore = semaphore->semaphore,
        .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT,
    }), &syncobj_fd));

    unix_check<drmSyncobjFDToHandle>(gpu->drm.fd, syncobj_fd, &semaphore->syncobj);

    close(syncobj_fd);

    return semaphore;
}

ref<gpu_semaphore> gpu_semaphore_import_syncobj(gpu_context* gpu, int syncobj_fd)
{
    auto semaphore = create_semaphore_base(gpu);

    unix_check<drmSyncobjFDToHandle>(gpu->drm.fd, syncobj_fd, &semaphore->syncobj);

    if (gpu_check(gpu->vk.ImportSemaphoreFdKHR(gpu->device, ptr_to(VkImportSemaphoreFdInfoKHR {
        .sType = VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_FD_INFO_KHR,
        .semaphore = semaphore->semaphore,
        .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT,
        .fd = syncobj_fd,
    }))) != VK_SUCCESS) {
        close(syncobj_fd);
    };

    return semaphore;
}

int gpu_semaphore_export_syncobj(gpu_semaphore* semaphore)
{
    int fd = -1;
    unix_check<drmSyncobjHandleToFD>(semaphore->gpu->drm.fd, semaphore->syncobj, &fd);
    return fd;
}

void gpu_semaphore_import_syncfile(gpu_semaphore* semaphore, int sync_fd, u64 target_point)
{
    auto* gpu = semaphore->gpu;

    if (sync_fd == -1) {
        unix_check<drmSyncobjTimelineSignal>(gpu->drm.fd, &semaphore->syncobj, &target_point, 1);
        return;
    }

    // We can't import from a syncfile directly to a syncobj point, so we import to a non-timeline syncobj first
    // and then transfer to our target point from that.

    u32 syncobj = {};
    unix_check<drmSyncobjCreate>(gpu->drm.fd, 0, &syncobj);
    defer { unix_check<drmSyncobjDestroy>(gpu->drm.fd, syncobj); };
    unix_check<drmSyncobjImportSyncFile>(gpu->drm.fd, syncobj, sync_fd);
    unix_check<drmSyncobjTransfer>(gpu->drm.fd, semaphore->syncobj, target_point, syncobj, 0, 0);
}

int gpu_semaphore_export_syncfile(gpu_semaphore* semaphore, u64 source_point)
{
    auto* gpu = semaphore->gpu;

    // We can't export directly from a timeline syncobj to a syncfile, so we transfer to a non-timeline syncobj first
    // and then export the syncfile from that.

    u32 syncobj = {};
    unix_check<drmSyncobjCreate>(gpu->drm.fd, 0, &syncobj);
    defer { unix_check<drmSyncobjDestroy>(gpu->drm.fd, syncobj); };
    unix_check<drmSyncobjTransfer>(gpu->drm.fd, syncobj, 0, semaphore->syncobj, source_point, 0);
    int sync_fd = -1;
    unix_check<drmSyncobjExportSyncFile>(gpu->drm.fd, syncobj, &sync_fd);

    return sync_fd;
}

gpu_semaphore::~gpu_semaphore()
{
    while (!wait.list.empty()) {
        wait.skips++;
        delete wait.list.first().remove().get();
    }

    gpu->vk.DestroySemaphore(gpu->device, semaphore, nullptr);
    unix_check<drmSyncobjDestroy>(gpu->drm.fd, syncobj);
}

u64 gpu_semaphore_get_value(gpu_semaphore* semaphore)
{
    auto* gpu = semaphore->gpu;

    u64 value = 0;
    gpu_check(gpu->vk.GetSemaphoreCounterValue(gpu->device, semaphore->semaphore, &value));

    return value;
}

static
void handle_waits(gpu_semaphore* semaphore)
{
    auto count = core_eventfd_read(semaphore->wait.fd.get());

    if (semaphore->gpu->features.contains(gpu_feature::validation)) {
        // Validation layers need to see the new semaphore value.
        auto _ = gpu_semaphore_get_value(semaphore);
    }

    if (count > semaphore->wait.skips) {
        count -= semaphore->wait.skips;
        semaphore->wait.skips = 0;

        // Waits are always sorted by increasing point values.
        // This means that we can simply pop the first N values
        // and know that they *must* have been reached. Regardless
        // of the order that events are actually signalled in.
        for (u32 i = 0; i < count; ++i) {
            auto w = semaphore->wait.list.first();
            core_assert(w != semaphore->wait.list.end());
            w.remove()->handle(w->point);
            delete w.get();
        }
    } else {
        semaphore->wait.skips -= count;
    }
}

void gpu_semaphore_wait(gpu_semaphore* semaphore, gpu_wait_fn* wait)
{
    auto* gpu = semaphore->gpu;

    if (!semaphore->wait.fd) {
        semaphore->wait.fd = core_fd_adopt(eventfd(0, EFD_CLOEXEC));

        core_fd_add_listener(semaphore->wait.fd.get(), gpu->event_loop.get(), core_fd_event_bit::readable,
            [semaphore](int, core_fd_event_bits) {
                handle_waits(semaphore);
            });
    }

    // Insert sorted into list
    auto cur = semaphore->wait.list.last();
    for (; cur != semaphore->wait.list.end() && cur->point > wait->point; cur = cur.prev());
    cur.insert_after(wait);

    unix_check<drmIoctl>(gpu->drm.fd, DRM_IOCTL_SYNCOBJ_EVENTFD, ptr_to(drm_syncobj_eventfd {
        .handle = semaphore->syncobj,
        .point = wait->point,
        .fd = semaphore->wait.fd.get(),
    }));
}

void gpu_wait(gpu_syncpoint syncpoint)
{
    auto[semaphore, value, stages] = syncpoint;
    auto* gpu = semaphore->gpu;

    gpu_check(gpu->vk.WaitSemaphores(gpu->device, ptr_to(VkSemaphoreWaitInfo {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
        .semaphoreCount = 1,
        .pSemaphores = &semaphore->semaphore,
        .pValues = &value,
    }), UINT64_MAX));

    if (std::this_thread::get_id() == gpu->event_loop->main_thread) {
        decltype(semaphore->wait.list)::iterator w;
        while (w = semaphore->wait.list.first(), w != semaphore->wait.list.end() && w->point <= value) {
            semaphore->wait.skips++;
            w.remove()->handle(value);
            delete w.get();
        }
    }
}

void gpu_semaphore_signal_value(gpu_semaphore* semaphore, u64 value)
{
    auto* gpu = semaphore->gpu;

    gpu_check(gpu->vk.SignalSemaphore(gpu->device, ptr_to(VkSemaphoreSignalInfo {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO,
        .semaphore = semaphore->semaphore,
        .value = value,
    })));
}
