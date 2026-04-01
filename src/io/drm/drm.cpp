#include "drm.hpp"

#include "../session/session.hpp"

#include "core/chrono.hpp"

// -----------------------------------------------------------------------------

static
void add_output(IoContext* io, IoDrmResources* resources, drmModeConnector* connector)
{
    /*
     * TODO: In the interest of fast prototyping, we will initially just re-use the existing
     *       configuration that we find, which should be that of the current TTY that we are
     *       being launched from.
     *
     *       In the future, we will want to handle reconfiguration of all DRM resources.
     */

    // Find encoder for this connector

    if (!connector->encoder_id) {
        log_warn("Connector {} has no encoder", connector->connector_id);
        return;
    }
    auto encoder = resources->find_encoder(connector->encoder_id);
    debug_assert(encoder);

    // Find CRTC currently used by this connector

    if (!encoder->crtc_id) {
        log_warn("Connector {} has no active CRTC", connector->connector_id);
        return;
    }
    auto* crtc = resources->find_crtc(encoder->crtc_id);
    debug_assert(crtc);

    // Ensure the CRTC is active

    if (!crtc->buffer_id) {
        log_warn("Connector is not active", connector->connector_id);
        return;
    }

    // Find plane currently used by CRTC

    drmModePlane* plane = nullptr;
    for (auto[i, p] : resources->planes | std::views::enumerate) {
        if (p->crtc_id == crtc->crtc_id && p->fb_id == crtc->buffer_id) {
            plane = p;
        }
    }
    debug_assert(plane);

    // Compute refresh rate

    u64 refresh = ((crtc->mode.clock * 1000000ul / crtc->mode.htotal) + (crtc->mode.vtotal / 2)) / crtc->mode.vtotal;

    log_warn("Creating output");
    log_warn("  crtc: {}", crtc->crtc_id);
    log_warn("  conn: {}", connector->connector_id);
    log_warn("  plane: {}", plane->plane_id);
    log_warn("  refresh: {} mHz", refresh);
    log_warn("  extent: ({}, {})", crtc->width, crtc->height);

    auto output = ref_create<IoDrmOutput>();
    output->io = io;

    output->primary_plane_id = plane->plane_id;
    output->crtc_id = crtc->crtc_id;
    output->connector_id = connector->connector_id;

    output->plane_prop = IoDrmPropertyMap(io->drm->fd, plane->plane_id, DRM_MODE_OBJECT_PLANE);
    output->crtc_prop  = IoDrmPropertyMap(io->drm->fd, crtc->crtc_id,   DRM_MODE_OBJECT_CRTC);

    output->size = {crtc->width, crtc->height};
    output->format_set = parse_plane_formats(io, resources, plane);

    io->drm->outputs.emplace_back(output.get());
    io_output_add(output.get());
    io_output_post_configure(output.get());
    io_output_try_redraw_later(output.get());
}

// -----------------------------------------------------------------------------

static
void on_page_flip(int fd, u32 sequence, u32 tv_sec, u32 tv_usec, u32 crtc_id, void* data);

// -----------------------------------------------------------------------------

void io_drm_init(IoContext* io)
{
    if (!io->session) {
        return;
    }

    io->drm = ref_create<IoDrm>();

    {
        auto* device = io->gpu->drm.device;
        if (!(device->available_nodes & (1 << DRM_NODE_PRIMARY))) {
            log_error("No primary DRM node available for Gpu");
            return;
        }
        io->drm->fd = io_session_open_device(io->session.get(), device->nodes[DRM_NODE_PRIMARY]);
        debug_assert(fd_is_valid(io->drm->fd));
    }
    auto drm = io->drm->fd;

    exec_fd_listen(io->exec, io ->drm->fd, FdEventBit::readable, [](int fd, Flags<FdEventBit>) {
        drmHandleEvent(fd, ptr_to(drmEventContext {
            .version = 3,
            .page_flip_handler2 = on_page_flip,
        }));
    });

    // Authenticate and check capabilities

    drm_magic_t magic;
    log_debug("Getting magic");
    unix_check<drmGetMagic>(drm, &magic);
    log_debug("Authenticating magic");
    unix_check<drmAuthMagic>(drm, magic);

    log_debug("Setting universal planes capability");
    unix_check<drmSetClientCap>(drm, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
    log_debug("Setting atomic capability");
    unix_check<drmSetClientCap>(drm, DRM_CLIENT_CAP_ATOMIC, 1);

    u64 cap = 0;

    log_debug("Checking for framebuffer modifier support");
    debug_assert(unix_check<drmGetCap>(drm, DRM_CAP_ADDFB2_MODIFIERS, &cap).ok() && cap);

    log_debug("Checking for monotonic timestamp support");
    debug_assert(unix_check<drmGetCap>(drm, DRM_CAP_TIMESTAMP_MONOTONIC, &cap).ok() && cap);
}

void io_drm_deinit(IoContext* io)
{
    io->drm.destroy();
}

void io_drm_start(IoContext* io)
{
    IoDrmResources res(io->drm->fd);

    for (auto* connector : res.connectors) {
        add_output(io, &res, connector);
    }
}

// -----------------------------------------------------------------------------

static
void on_page_flip(int fd, u32 sequence, u32 tv_sec, u32 tv_usec, u32 crtc_id, void* data)
{
    auto* output = static_cast<IoDrmOutput*>(data);

    output->current_image = output->pending_image;

    output->commit_available = true;
    io_output_try_redraw(output);
}

// -----------------------------------------------------------------------------

static
u32 get_image_fb2(IoContext* io, GpuImage* image)
{
    // We need to cache based on the underlying image, not any lease handle
    image = image->base();

    std::optional<u32> found = std::nullopt;
    std::erase_if(io->drm->buffer_cache, [&](const auto& entry) {
        if (!entry.image) {
            drmCloseBufferHandle(io->drm->fd, entry.fb2_handle);
            return true;
        }
        if (entry.image.get() == image) found = entry.fb2_handle;
        return false;
    });
    if (found) return *found;

    log_warn("Importing new FB2 buffer");

    auto dma_params = gpu_image_export(image);
    auto size = image->extent();
    auto format = image->format();

    // Acquire GEM handles and prepare for import

    u32 handles[4] = {};
    u32 pitches[4] = {};
    u32 offsets[4] = {};
    u64 modifiers[4] = {};
    for (u32 i = 0; i < dma_params.planes.count; ++i) {
        unix_check<drmPrimeFDToHandle>(io->drm->fd, dma_params.planes[i].fd.get(), &handles[i]);
        log_warn("  plane[{}] prime fd {} -> GEM handle {}", i, dma_params.planes[i].fd.get(), handles[i]);
        pitches[i] = dma_params.planes[i].stride;
        offsets[i] = dma_params.planes[i].offset;
        modifiers[i] = dma_params.modifier;
    }

    // Import

    u32 fb2_handle = 0;
    unix_check<drmModeAddFB2WithModifiers>(io->drm->fd,
        size.x, size.y,
        format->drm, handles, pitches, offsets, modifiers,
        &fb2_handle, DRM_MODE_FB_MODIFIERS);

    // Close GEM handles

    std::flat_set<u32> unique_handles;
    unique_handles.insert_range(handles);
    for (auto handle : unique_handles) drmCloseBufferHandle(io->drm->fd, handle);

    return io->drm->buffer_cache.emplace_back(image, fb2_handle).fb2_handle;
}

// -----------------------------------------------------------------------------

void IoDrmOutput::commit(
    GpuImage* image,
    GpuSyncpoint acquire,
    Flags<IoOutputCommitFlag> in_flags)
{
    debug_assert(commit_available);
    commit_available = false;

    auto fb2_handle = get_image_fb2(io, image);

    auto req = drmModeAtomicAlloc();
    defer { drmModeAtomicFree(req); };

    auto plane_set = [&](std::string_view name, u64 value) {
        drmModeAtomicAddProperty(req, primary_plane_id, plane_prop.get_prop_id(name), value);
    };

    auto in_fence = gpu_syncobj_export_syncfile(acquire.syncobj, acquire.value);

    plane_set("FB_ID", fb2_handle);
    plane_set("IN_FENCE_FD", in_fence.get());
    plane_set("SRC_X", 0);
    plane_set("SRC_Y", 0);
    plane_set("SRC_W", image->extent().x << 16);
    plane_set("SRC_H", image->extent().y << 16);
    plane_set("CRTC_X", 0);
    plane_set("CRTC_Y", 0);
    plane_set("CRTC_W", size.x);
    plane_set("CRTC_H", size.y);

    auto flags = DRM_MODE_ATOMIC_NONBLOCK | DRM_MODE_PAGE_FLIP_EVENT;

    drmModeAtomicAddProperty(req, crtc_id, crtc_prop.get_prop_id("VRR_ENABLED"), true);

    if (unix_check<drmModeAtomicCommit>(io->drm->fd, req, flags, this).err()) {
        debug_assert_fail("IoDrmOutput::commit", "TODO: FAILED TO COMMIT");
    }

    pending_image = image;
    last_commit_time = std::chrono::steady_clock::now();
}
