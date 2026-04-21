#include "internal.hpp"

auto gpu_syncpoint_to_submit_info(const GpuSyncpoint& syncpoint) -> VkSemaphoreSubmitInfo
{
    auto[syncobj, value, stages] = syncpoint;

    if (!syncobj->semaphore) {
        auto* gpu = syncobj->gpu;

        gpu_check(gpu->vk.CreateSemaphore(gpu->device, ptr_to(VkSemaphoreCreateInfo {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
            .pNext = ptr_to(VkSemaphoreTypeCreateInfo {
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
                .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
            })
        }), nullptr, &syncobj->semaphore));

        int syncobj_fd;
        unix_check<drmSyncobjHandleToFD>(gpu->drm.fd, syncobj->syncobj, &syncobj_fd);

        if (gpu_check(gpu->vk.ImportSemaphoreFdKHR(gpu->device, ptr_to(VkImportSemaphoreFdInfoKHR {
            .sType = VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_FD_INFO_KHR,
            .semaphore = syncobj->semaphore,
            .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT,
            .fd = syncobj_fd,
        }))) != VK_SUCCESS) {
            close(syncobj_fd);
        };
    }

    return {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = syncobj->semaphore,
        .value     = value,
        .stageMask = stages,
    };
}

// -----------------------------------------------------------------------------

auto gpu_syncobj_create(Gpu* gpu) -> Ref<GpuSyncobj>
{
    auto syncobj = ref_create<GpuSyncobj>();
    syncobj->gpu = gpu;

    unix_check<drmSyncobjCreate>(gpu->drm.fd, 0, &syncobj->syncobj);

    return syncobj;
}

auto gpu_syncobj_import(Gpu* gpu, int syncobj_fd) -> Ref<GpuSyncobj>
{
    auto syncobj = ref_create<GpuSyncobj>();
    syncobj->gpu = gpu;

    unix_check<drmSyncobjFDToHandle>(gpu->drm.fd, syncobj_fd, &syncobj->syncobj);

    return syncobj;
}

auto gpu_syncobj_export(GpuSyncobj* syncobj) -> Fd
{
    int fd = -1;
    unix_check<drmSyncobjHandleToFD>(syncobj->gpu->drm.fd, syncobj->syncobj, &fd);
    return Fd(fd);
}

void gpu_syncobj_import_syncfile(GpuSyncobj* syncobj, u64 target_point, int sync_fd)
{
    auto* gpu = syncobj->gpu;

    if (sync_fd == -1) {
        unix_check<drmSyncobjTimelineSignal>(gpu->drm.fd, &syncobj->syncobj, &target_point, 1);
        return;
    }

    // We can't import from a syncfile directly to a timeline syncobj point, so we import to a temporary binary syncobj first
    // and then transfer to our target point from that.

    unix_check<drmSyncobjImportSyncFile>(gpu->drm.fd, gpu->drm.syncobj, sync_fd);
    unix_check<drmSyncobjTransfer>(gpu->drm.fd, syncobj->syncobj, target_point, gpu->drm.syncobj, 0, 0);
}

auto gpu_syncobj_export_syncfile(GpuSyncobj* syncobj, u64 source_point) -> Fd
{
    auto* gpu = syncobj->gpu;

    // We can't export directly from a timeline syncobj point to a syncfile, so we transfer to a temporary binary syncobj first
    // and then export the syncfile from that.

    unix_check<drmSyncobjTransfer>(gpu->drm.fd, gpu->drm.syncobj, 0, syncobj->syncobj, source_point, 0);
    int sync_fd = -1;
    unix_check<drmSyncobjExportSyncFile>(gpu->drm.fd, gpu->drm.syncobj, &sync_fd);

    return Fd(sync_fd);
}

GpuSyncobj::~GpuSyncobj()
{
    gpu->vk.DestroySemaphore(gpu->device, semaphore, nullptr);

    if (wait.fd) {
        exec_fd_unlisten(gpu->exec, wait.fd.get());
    }

    while (!wait.list.empty()) {
        wait.skips++;
        delete wait.list.first().remove().get();
    }

    unix_check<drmSyncobjDestroy>(gpu->drm.fd, syncobj);
}

auto gpu_syncobj_get_value(GpuSyncobj* syncobj) -> u64
{
    auto* gpu = syncobj->gpu;

    u64 value = 0;
    unix_check<drmSyncobjQuery>(gpu->drm.fd, &syncobj->syncobj, &value, 1);

    return value;
}

static
void handle_waits(GpuSyncobj* syncobj)
{
    eventfd_t count = {};
    unix_check<eventfd_read>(syncobj->wait.fd.get(), &count);

#if GPU_VALIDATION_COMPATIBILITY
    // Validation layers need to see the new semaphore value.
    auto* gpu = syncobj->gpu;
    if (syncobj->semaphore && gpu->features.contains(GpuFeature::validation)) {
        u64 value = 0;
        gpu_check(gpu->vk.GetSemaphoreCounterValue(gpu->device, syncobj->semaphore, &value));
    }
#endif

    if (count > syncobj->wait.skips) {
        count -= syncobj->wait.skips;
        syncobj->wait.skips = 0;

        // Waits are always sorted by increasing point values.
        // This means that we can simply pop the first N values
        // and know that they *must* have been reached. Regardless
        // of the order that events are actually signalled in.
        for (u32 i = 0; i < count; ++i) {
            auto w = syncobj->wait.list.first();
            debug_assert(w != syncobj->wait.list.end());
            w.remove()->handle(w->point);
            delete w.get();
        }
    } else {
        syncobj->wait.skips -= count;
    }
}

void gpu_syncobj_wait(GpuSyncobj* syncobj, GpuWaitFn* wait)
{
    auto* gpu = syncobj->gpu;

    if (!syncobj->wait.fd) {
        syncobj->wait.fd = Fd(eventfd(0, EFD_CLOEXEC));

        exec_fd_listen(gpu->exec, syncobj->wait.fd.get(), FdEventBit::readable,
            [syncobj](int, Flags<FdEventBit>) {
                handle_waits(syncobj);
            });
    }

    // Insert sorted into list
    auto cur = syncobj->wait.list.last();
    for (; cur != syncobj->wait.list.end() && cur->point > wait->point; cur = cur.prev());
    cur.insert_after(wait);

    unix_check<drmIoctl>(gpu->drm.fd, DRM_IOCTL_SYNCOBJ_EVENTFD, ptr_to(drm_syncobj_eventfd {
        .handle = syncobj->syncobj,
        .point = wait->point,
        .fd = syncobj->wait.fd.get(),
    }));
}

void gpu_wait(GpuSyncpoint syncpoint)
{
    auto[syncobj, value, stages] = syncpoint;
    auto* gpu = syncobj->gpu;

    u32 first_signalled;
    unix_check<drmSyncobjTimelineWait>(gpu->drm.fd, &syncobj->syncobj, &value, 1, INT64_MAX, 0, &first_signalled);

    if (std::this_thread::get_id() == gpu->exec->os_thread) {
        decltype(syncobj->wait.list)::Iterator w;
        while (w = syncobj->wait.list.first(), w != syncobj->wait.list.end() && w->point <= value) {
            syncobj->wait.skips++;
            w.remove()->handle(value);
            delete w.get();
        }
    }
}

void gpu_syncobj_signal_value(GpuSyncobj* syncobj, u64 value)
{
    auto* gpu = syncobj->gpu;

    unix_check<drmSyncobjTimelineSignal>(gpu->drm.fd, &syncobj->syncobj, &value, 1);
}

// -----------------------------------------------------------------------------

GpuBinarySemaphore::~GpuBinarySemaphore()
{
    gpu->free_binary_semaphores.emplace_back(semaphore);
}

auto gpu_get_binary_semaphore(Gpu* gpu) -> Ref<GpuBinarySemaphore>
{
    auto binary = ref_create<GpuBinarySemaphore>();
    binary->gpu = gpu;

    if (gpu->free_binary_semaphores.empty()) {
        log_warn("Allocating new binary semaphore");
        gpu_check(gpu->vk.CreateSemaphore(gpu->device, ptr_to(VkSemaphoreCreateInfo {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
            .pNext = ptr_to(VkExportSemaphoreCreateInfo {
                .sType = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO,
                .handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT,
            }),
        }), nullptr, &binary->semaphore));
    } else {
        binary->semaphore = gpu->free_binary_semaphores.back();
        gpu->free_binary_semaphores.pop_back();
    }

    return binary;
}
