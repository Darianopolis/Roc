#include "drm.hpp"
#include "debug.hpp"

#include "../session/session.hpp"

#include <core/chrono.hpp>
#include <core/log.hpp>

// -----------------------------------------------------------------------------

static
auto parse_plane_formats(IoContext* io, IoDrmPlane* plane) -> GpuFormatSet
{
    auto drm = io->drm->fd;

    auto blob_id = plane->get("IN_FORMATS");
    if (!blob_id) {
        log_error("Plane has no IN_FORMATS property");
        return {};
    }

    auto blob = unix_check<drmModeGetPropertyBlob>(drm, blob_id).value;
    defer { drmModeFreePropertyBlob(blob); };

    GpuFormatSet set;
    if (!blob) return set;

    auto* header = static_cast<drm_format_modifier_blob*>(blob->data);

    auto* formats = byte_offset_pointer<GpuDrmFormat>(blob->data, header->formats_offset);
    auto* modifiers = byte_offset_pointer<drm_format_modifier>(blob->data, header->modifiers_offset);

    for (auto mod : std::span(modifiers, header->count_modifiers)) {
        u32 index = mod.offset;
        while (mod.formats) {
            auto bit_idx = std::countr_zero(mod.formats);
            index += bit_idx;

            auto format = gpu_format_from_drm(formats[index]);
            if (format) set.add(format, mod.modifier);

            mod.formats >>= bit_idx + 1;
            index++;
        }
    }

    return set;
}

static
void try_add_output(IoContext* io, IoDrmConnector* connector)
{
    /*
     * TODO: In the interest of fast prototyping, we will initially just re-use the existing
     *       configuration that we find, which should be that of the current TTY that we are
     *       being launched from.
     *
     *       In the future, we will want to handle reconfiguration of all DRM resources.
     */

    // Find encoder for this connector

    auto* encoder = io_drm_find_encoder(io, connector->info->encoder_id);
    if (!encoder) {
        log_warn("Connector {} has no encoder", connector->info->connector_id);
        return;
    }

    // Find CRTC currently used by this connector

    auto* crtc = io_drm_find_crtc(io, encoder->info->crtc_id);
    if (!crtc) {{
        log_warn("Connector {} has no active CRTC", connector->id);
        return;
    }}

    // Ensure the CRTC is active

    if (!crtc->info->buffer_id) {
        log_warn("Connector is not active", connector->id);
        return;
    }

    auto output = ref_create<IoDrmOutput>();
    output->io = io;

    // Find primary and cursor planes

    for (auto* p : io->drm->planes) {
        if (std::popcount(p->info->possible_crtcs) != 1) {
            // TODO: Handle cross-crtc plane allocation
            continue;
        }
        if (!(p->info->possible_crtcs & (1 << crtc->index))) continue;
        switch (p->get("type")) {
            break;case DRM_PLANE_TYPE_PRIMARY:
                output->primary_plane = p;
            break;case DRM_PLANE_TYPE_CURSOR:
                output->cursor_plane = p;
        }
    }
    debug_assert(output->primary_plane);

    // Compute refresh rate

    u64 refresh = ((crtc->info->mode.clock * 1000000ul / crtc->info->mode.htotal) + (crtc->info->mode.vtotal / 2)) / crtc->info->mode.vtotal;

    log_warn("Creating output");
    log_warn("  crtc: {}", crtc->id);
    log_warn("  conn: {}", connector->id);
    log_warn("  primary plane: {}", output->primary_plane->id);
    log_warn("  cursor plane: {}", output->cursor_plane->id);
    log_warn("  refresh: {} mHz", refresh);
    log_warn("  extent: ({}, {})", crtc->info->width, crtc->info->height);

    output->crtc = crtc;
    output->connector = connector;

    output->size = {crtc->info->width, crtc->info->height};
    output->formats = parse_plane_formats(io, output->primary_plane);

    io->drm->outputs.emplace_back(output.get());
    io_output_add(output.get());
    io_output_post_configure(output.get());
    io_output_try_redraw_later(output.get());
}

void io_drm_configure_outputs(IoContext* io)
{
    io->drm->outputs.erase_if([](IoDrmOutput* output) { return !output->connector; });

    for (auto* connector : io->drm->connectors) {
        if (std::ranges::any_of(io->drm->outputs,
                [&](auto* output) {
                    return output->connector.get() == connector;
                })) {
            // Connector is already used in any existing output
            continue;
        }
        try_add_output(io, connector);
    }
}

// -----------------------------------------------------------------------------

static
void on_page_flip(fd_t fd, u32 sequence, u32 tv_sec, u32 tv_usec, u32 crtc_id, void* data);

// -----------------------------------------------------------------------------

static
void on_session_state(IoContext* io, bool enabled)
{
    if (enabled) {
        log_warn("Resuming DRM session");
        io_drm_configure_outputs(io);
    } else {
        log_warn("Suspending DRM session");
        io->drm->outputs.clear();
    }
}

void io_drm_init(IoContext* io)
{
    if (!io->session) {
        return;
    }

    io->drm = ref_create<IoDrm>();

    io->drm->session_listener = io->session->signals.state.listen([io](bool enabled) {
        on_session_state(io, enabled);
    });

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

    fd_listen(io->exec, io->drm->fd, FdEventBit::readable, [](fd_t fd, Flags<FdEventBit>) {
        unix_check<drmHandleEvent>(fd, ptr_to(drmEventContext {
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
    if (!io->drm) return;

    fd_unlisten(io->exec, io->drm->fd);
    io_session_close_device(io->session.get(), io->drm->fd);

    io->drm->outputs.destroy_all();
    io->drm.destroy();
}

void io_drm_start(IoContext* io)
{
    io_drm_enumerate_static_objects(io);
    io_drm_enumerate_connectors(io);
    io_drm_configure_outputs(io);
}

// -----------------------------------------------------------------------------

// TODO: This is a hack so that we can smuggle a weak pointer in the space of a raw pointer,
//       unlike Weak<T> itself which also stores a 64 bit offset
//
//       We should probably implement some form of this generically in core
//
struct alignas(void*) IoDrmWeakOutput
{
    Allocation allocation;
    AllocationVersion version;

    IoDrmWeakOutput() = default;

    IoDrmWeakOutput(IoDrmOutput* output)
        : allocation(allocation_from(output))
        , version(allocation_get_version(allocation))
    {}

    auto get() const -> IoDrmOutput*
    {
        if (allocation_get_version(allocation) != version) return nullptr;
        return static_cast<IoDrmOutput*>(allocation_get_data(allocation));
    }
};

static
void on_page_flip(fd_t fd, u32 sequence, u32 tv_sec, u32 tv_usec, u32 crtc_id, void* data)
{
    auto output = std::bit_cast<IoDrmWeakOutput>(data).get();

    if (!output) {
        log_warn("DRM : Output was destroyed before page flip processed, discarding...");
        return;
    }

    output->current_image = output->pending_image;
    output->current_cursor_image = output->pending_cursor_image;

    output->commit_available = true;
    io_output_try_redraw(output);
}

// -----------------------------------------------------------------------------

static
auto get_image_framebuffer(IoContext* io, GpuImageBase* image) -> u32
{
    std::optional<u32> found = std::nullopt;
    std::erase_if(io->drm->buffer_cache, [&](const auto& entry) {
        if (!entry.image) {
            log_warn("KMS : Retiring old framebuffer");
            unix_check<drmModeCloseFB>(io->drm->fd, entry.framebuffer);
            return true;
        }
        if (entry.image.get() == image) found = entry.framebuffer;
        return false;
    });
    if (found) return *found;

    log_warn("KMS : Importing new framebuffer");

    auto dma_params = gpu_image_export(image);
    auto size = image->extent;
    auto format = image->format;

    // Acquire GEM handles and prepare for import

    u32 handles[4] = {};
    u32 pitches[4] = {};
    u32 offsets[4] = {};
    u64 modifiers[4] = {};
    for (u32 i = 0; i < dma_params.planes.count; ++i) {
        unix_check<drmPrimeFDToHandle>(io->drm->fd, dma_params.planes[i].fd.get(), &handles[i]);
        log_warn("KMS :   plane[{}] prime fd {} -> GEM handle {}", i, dma_params.planes[i].fd.get(), handles[i]);
        pitches[i] = dma_params.planes[i].stride;
        offsets[i] = dma_params.planes[i].offset;
        modifiers[i] = dma_params.modifier;
    }

    // Import

    u32 framebuffer = 0;
    unix_check<drmModeAddFB2WithModifiers>(io->drm->fd,
        size.x, size.y,
        format->drm, handles, pitches, offsets, modifiers,
        &framebuffer, DRM_MODE_FB_MODIFIERS);
    log_warn("KMS :   framebuffer: {}", framebuffer);

    // Close GEM handles
    //
    // NOTE: Each PRIME buffer only has a single GEM handle slot that is *NOT* reference counted
    //       Therefore we must duplicate the handles to avoid double-closing

    std::flat_set<u32> unique_handles;
    unique_handles.insert_range(handles | std::views::filter([](u32 v) { return v; }));
    for (auto handle : unique_handles) {
        unix_check<drmCloseBufferHandle>(io->drm->fd, handle);
    }

    return io->drm->buffer_cache.emplace_back(image, framebuffer).framebuffer;
}

// -----------------------------------------------------------------------------

auto IoDrmOutput::commit(const WmOutputCommitInfo& commit) -> bool
{
    debug_assert(commit_available);
    commit_available = false;

    auto req = unix_check<drmModeAtomicAlloc>().value;
    defer { drmModeAtomicFree(req); };

    auto in_fence = gpu_syncobj_export_syncfile(commit.ready.syncobj, commit.ready.value);

    primary_plane->set(req, "FB_ID", get_image_framebuffer(io, commit.primary.image->base()));
    primary_plane->set(req, "IN_FENCE_FD", in_fence.get());
    primary_plane->set(req, "SRC_X", 0);
    primary_plane->set(req, "SRC_Y", 0);
    primary_plane->set(req, "SRC_W", commit.primary.image->base()->extent.x << 16);
    primary_plane->set(req, "SRC_H", commit.primary.image->base()->extent.y << 16);
    primary_plane->set(req, "CRTC_X", 0);
    primary_plane->set(req, "CRTC_Y", 0);
    primary_plane->set(req, "CRTC_W", size.x);
    primary_plane->set(req, "CRTC_H", size.y);
    primary_plane->set(req, "CRTC_ID", crtc->id);

    if (commit.cursor.image) {
        cursor_plane->set(req, "FB_ID", get_image_framebuffer(io, commit.cursor.image->base()));
        cursor_plane->set(req, "IN_FENCE_FD", in_fence.get());
        cursor_plane->set(req, "SRC_X", 0);
        cursor_plane->set(req, "SRC_Y", 0);
        cursor_plane->set(req, "SRC_W", commit.cursor.image->base()->extent.x << 16);
        cursor_plane->set(req, "SRC_H", commit.cursor.image->base()->extent.y << 16);
        cursor_plane->set(req, "CRTC_X", commit.cursor.position.x);
        cursor_plane->set(req, "CRTC_Y", commit.cursor.position.y);
        cursor_plane->set(req, "CRTC_W", commit.cursor.image->base()->extent.x);
        cursor_plane->set(req, "CRTC_H", commit.cursor.image->base()->extent.y);
        cursor_plane->set(req, "CRTC_ID", crtc->id);
    } else {
        cursor_plane->set(req, "FB_ID", 0);
        cursor_plane->set(req, "CRTC_ID", 0);
    }

    crtc->set(req, "VRR_ENABLED", connector->get("vrr_capable"));

    auto flags = DRM_MODE_ATOMIC_NONBLOCK | DRM_MODE_PAGE_FLIP_EVENT;

    if (unix_check<drmModeAtomicCommit>(io->drm->fd, req, flags, std::bit_cast<void*>(IoDrmWeakOutput{this})).err()) {
        return false;
    }

    pending_image = commit.primary.image;
    pending_cursor_image = commit.cursor.image;
    last_commit_time = std::chrono::steady_clock::now();
    return true;
}
