#include "internal.hpp"

ref<gpu_syncobj> gpu_syncobj_create(gpu_context* gpu)
{
    auto syncobj = core_create<gpu_syncobj>();
    syncobj->gpu = gpu;

    unix_check<drmSyncobjCreate>(gpu->drm.fd, 0, &syncobj->syncobj);

    return syncobj;
}

ref<gpu_syncobj> gpu_syncobj_import(gpu_context* gpu, int syncobj_fd)
{
    auto syncobj = core_create<gpu_syncobj>();
    syncobj->gpu = gpu;

    unix_check<drmSyncobjFDToHandle>(gpu->drm.fd, syncobj_fd, &syncobj->syncobj);

    return syncobj;
}

auto gpu_syncobj_export(gpu_syncobj* syncobj) -> core_fd
{
    int fd = -1;
    unix_check<drmSyncobjHandleToFD>(syncobj->gpu->drm.fd, syncobj->syncobj, &fd);
    return core_fd(fd);
}

void gpu_syncobj_import_syncfile(gpu_syncobj* syncobj, u64 target_point, int sync_fd)
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

auto gpu_syncobj_export_syncfile(gpu_syncobj* syncobj, u64 source_point) -> core_fd
{
    auto* gpu = syncobj->gpu;

    // We can't export directly from a timeline syncobj point to a syncfile, so we transfer to a temporary binary syncobj first
    // and then export the syncfile from that.

    unix_check<drmSyncobjTransfer>(gpu->drm.fd, gpu->drm.syncobj, 0, syncobj->syncobj, source_point, 0);
    int sync_fd = -1;
    unix_check<drmSyncobjExportSyncFile>(gpu->drm.fd, gpu->drm.syncobj, &sync_fd);

    return core_fd(sync_fd);
}

gpu_syncobj::~gpu_syncobj()
{
    if (wait.fd) {
        exec_fd_unlisten(gpu->exec, wait.fd.get());
    }

    while (!wait.list.empty()) {
        wait.skips++;
        delete wait.list.first().remove().get();
    }

    unix_check<drmSyncobjDestroy>(gpu->drm.fd, syncobj);
}

u64 gpu_syncobj_get_value(gpu_syncobj* syncobj)
{
    auto* gpu = syncobj->gpu;

    u64 value = 0;
    unix_check<drmSyncobjQuery>(gpu->drm.fd, &syncobj->syncobj, &value, 1);

    return value;
}

static
void handle_waits(gpu_syncobj* syncobj)
{
    eventfd_t count = {};
    unix_check<eventfd_read>(syncobj->wait.fd.get(), &count);

    if (count > syncobj->wait.skips) {
        count -= syncobj->wait.skips;
        syncobj->wait.skips = 0;

        // Waits are always sorted by increasing point values.
        // This means that we can simply pop the first N values
        // and know that they *must* have been reached. Regardless
        // of the order that events are actually signalled in.
        for (u32 i = 0; i < count; ++i) {
            auto w = syncobj->wait.list.first();
            core_assert(w != syncobj->wait.list.end());
            w.remove()->handle(w->point);
            delete w.get();
        }
    } else {
        syncobj->wait.skips -= count;
    }
}

void gpu_syncobj_wait(gpu_syncobj* syncobj, gpu_wait_fn* wait)
{
    auto* gpu = syncobj->gpu;

    if (!syncobj->wait.fd) {
        syncobj->wait.fd = core_fd(eventfd(0, EFD_CLOEXEC));

        exec_fd_listen(gpu->exec, syncobj->wait.fd.get(), exec_fd_event_bit::readable,
            [syncobj](int, flags<exec_fd_event_bit>) {
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

void gpu_wait(gpu_syncpoint syncpoint)
{
    auto[syncobj, value, stages] = syncpoint;
    auto* gpu = syncobj->gpu;

    u32 first_signalled;
    unix_check<drmSyncobjTimelineWait>(gpu->drm.fd, &syncobj->syncobj, &value, 1, INT64_MAX, 0, &first_signalled);

    if (std::this_thread::get_id() == gpu->exec->os_thread) {
        decltype(syncobj->wait.list)::iterator w;
        while (w = syncobj->wait.list.first(), w != syncobj->wait.list.end() && w->point <= value) {
            syncobj->wait.skips++;
            w.remove()->handle(value);
            delete w.get();
        }
    }
}

void gpu_syncobj_signal_value(gpu_syncobj* syncobj, u64 value)
{
    auto* gpu = syncobj->gpu;

    unix_check<drmSyncobjTimelineSignal>(gpu->drm.fd, &syncobj->syncobj, &value, 1);
}

// -----------------------------------------------------------------------------

gpu_binary_semaphore::~gpu_binary_semaphore()
{
    gpu->free_binary_semaphores.emplace_back(semaphore);
}

auto gpu_get_binary_semaphore(gpu_context* gpu) -> ref<gpu_binary_semaphore>
{
    auto binary = core_create<gpu_binary_semaphore>();
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
