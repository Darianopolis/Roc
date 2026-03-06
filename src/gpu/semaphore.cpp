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
ref<gpu_semaphore> create_semaphore_base(gpu_context* ctx)
{
    auto semaphore = core_create<gpu_semaphore>();
    semaphore->ctx = ctx;

    gpu_check(ctx->vk.CreateSemaphore(ctx->device, ptr_to(VkSemaphoreCreateInfo {
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

ref<gpu_semaphore> gpu_semaphore_create(gpu_context* ctx)
{
    // Here we are creating a timeline sempahore, and exporting a persistent syncobj
    // handle to it that we can use for importing/exporting syncobj files for interop.
    // This trick only works when the driver uses syncobjs as the opaque fd type.
    // This seems to work fine on AMD, but definitely won't work for all vendors.

    // As a portable solution, we'll have to use DRM syncobjs as the underlying type, creating
    // binary semaphores as required for vulkan waits

    auto semaphore = create_semaphore_base(ctx);

    int syncobj_fd = -1;
    gpu_check(ctx->vk.GetSemaphoreFdKHR(ctx->device, ptr_to(VkSemaphoreGetFdInfoKHR {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR,
        .semaphore = semaphore->semaphore,
        .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT,
    }), &syncobj_fd));

    unix_check(drmSyncobjFDToHandle(ctx->drm.fd, syncobj_fd, &semaphore->syncobj));

    close(syncobj_fd);

    return semaphore;
}

ref<gpu_semaphore> gpu_semaphore_import_syncobj(gpu_context* ctx, int syncobj_fd)
{
    auto semaphore = create_semaphore_base(ctx);

    unix_check(drmSyncobjFDToHandle(ctx->drm.fd, syncobj_fd, &semaphore->syncobj));

    if (gpu_check(ctx->vk.ImportSemaphoreFdKHR(ctx->device, ptr_to(VkImportSemaphoreFdInfoKHR {
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
    unix_check(drmSyncobjHandleToFD(semaphore->ctx->drm.fd, semaphore->syncobj, &fd));
    return fd;
}

void gpu_semaphore_import_syncfile(gpu_semaphore* semaphore, int sync_fd, u64 target_point)
{
    auto* ctx = semaphore->ctx;

    if (sync_fd == -1) {
        unix_check(drmSyncobjTimelineSignal(ctx->drm.fd, &semaphore->syncobj, &target_point, 1));
        return;
    }

    // We can't import from a syncfile directly to a syncobj point, so we import to a non-timeline syncobj first
    // and then transfer to our target point from that.

    u32 syncobj = {};
    unix_check(drmSyncobjCreate(ctx->drm.fd, 0, &syncobj));
    defer { unix_check(drmSyncobjDestroy(ctx->drm.fd, syncobj)); };
    unix_check(drmSyncobjImportSyncFile(ctx->drm.fd, syncobj, sync_fd));
    unix_check(drmSyncobjTransfer(ctx->drm.fd, semaphore->syncobj, target_point, syncobj, 0, 0));
}

int gpu_semaphore_export_syncfile(gpu_semaphore* semaphore, u64 source_point)
{
    auto* ctx = semaphore->ctx;

    // We can't export directly from a timeline syncobj to a syncfile, so we transfer to a non-timeline syncobj first
    // and then export the syncfile from that.

    u32 syncobj = {};
    unix_check(drmSyncobjCreate(ctx->drm.fd, 0, &syncobj));
    defer { unix_check(drmSyncobjDestroy(ctx->drm.fd, syncobj)); };
    unix_check(drmSyncobjTransfer(ctx->drm.fd, syncobj, 0, semaphore->syncobj, source_point, 0));
    int sync_fd = -1;
    unix_check(drmSyncobjExportSyncFile(ctx->drm.fd, syncobj, &sync_fd));

    return sync_fd;
}

gpu_semaphore::~gpu_semaphore()
{
    while (!waits.empty()) {
        wait_skips++;
        delete waits.first().remove().get();
    }

    ctx->vk.DestroySemaphore(ctx->device, semaphore, nullptr);
    unix_check(drmSyncobjDestroy(ctx->drm.fd, syncobj));
}

u64 gpu_semaphore_get_value(gpu_semaphore* semaphore)
{
    auto* ctx = semaphore->ctx;

    u64 value = 0;
    gpu_check(ctx->vk.GetSemaphoreCounterValue(ctx->device, semaphore->semaphore, &value));

    return value;
}

static
void handle_waits(gpu_semaphore* semaphore)
{
    auto count = core_eventfd_read(semaphore->wait_fd.get());

    if (semaphore->ctx->features.contains(gpu_feature::validation)) {
        // Validation layers need to see the new semaphore value.
        auto _ = gpu_semaphore_get_value(semaphore);
    }

    if (count > semaphore->wait_skips) {
        count -= semaphore->wait_skips;
        semaphore->wait_skips = 0;

        // Waits are always sorted by increasing point values.
        // This means that we can simply pop the first N values
        // and know that they *must* have been reached. Regardless
        // of the order that events are actually signalled in.
        for (u32 i = 0; i < count; ++i) {
            auto w = semaphore->waits.first();
            core_assert(w != semaphore->waits.end());
            w.remove()->handle(w->point);
            delete w.get();
        }
    } else {
        semaphore->wait_skips -= count;
    }
}

void gpu_semaphore_wait_value_impl(gpu_semaphore* semaphore, gpu_semaphore::wait_item* wait)
{
    auto* ctx = semaphore->ctx;

    if (!semaphore->wait_fd) {
        semaphore->wait_fd = core_fd_adopt(eventfd(0, EFD_CLOEXEC));

        core_fd_add_listener(semaphore->wait_fd.get(), ctx->event_loop.get(), core_fd_event_bit::readable,
            [semaphore](int, core_fd_event_bits) {
                handle_waits(semaphore);
            });
    }

    // Insert sorted into list
    auto cur = semaphore->waits.last();
    for (; cur != semaphore->waits.end() && cur->point > wait->point; cur = cur.prev());
    cur.insert_after(wait);

    unix_check(drmIoctl(ctx->drm.fd, DRM_IOCTL_SYNCOBJ_EVENTFD, ptr_to(drm_syncobj_eventfd {
        .handle = semaphore->syncobj,
        .point = wait->point,
        .fd = semaphore->wait_fd.get(),
    })));
}

void gpu_semaphore_wait_value(gpu_semaphore* semaphore, u64 value)
{
    auto* ctx = semaphore->ctx;

    gpu_check(ctx->vk.WaitSemaphores(ctx->device, ptr_to(VkSemaphoreWaitInfo {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
        .semaphoreCount = 1,
        .pSemaphores = &semaphore->semaphore,
        .pValues = &value,
    }), UINT64_MAX));

    if (std::this_thread::get_id() == ctx->event_loop->main_thread) {
        decltype(semaphore->waits)::iterator w;
        while (w = semaphore->waits.first(), w != semaphore->waits.end() && w->point <= value) {
            semaphore->wait_skips++;
            w.remove()->handle(value);
            delete w.get();
        }
    }
}

void gpu_semaphore_signal_value(gpu_semaphore* semaphore, u64 value)
{
    auto* ctx = semaphore->ctx;

    gpu_check(ctx->vk.SignalSemaphore(ctx->device, ptr_to(VkSemaphoreSignalInfo {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO,
        .semaphore = semaphore->semaphore,
        .value = value,
    })));
}
