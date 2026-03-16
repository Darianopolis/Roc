#include "backend.hpp"

#include "core/chrono.hpp"

#include "wroc/event.hpp"

struct drm_resources
{
    std::vector<drmModeConnector*> connectors;
    std::vector<drmModeEncoder*> encoders;
    std::vector<drmModeCrtc*> crtcs;
    std::vector<drmModePlane*> planes;

    drm_resources(int drm_fd)
    {
        auto mode_res = unix_check<drmModeGetResources>(drm_fd).value;
        if (!mode_res) {
            log_warn("Failed to get mode resources");
            return;
        }
        defer { drmModeFreeResources(mode_res); };

        auto plane_res = unix_check<drmModeGetPlaneResources>(drm_fd).value;
        if (!plane_res) {
            log_warn("Failed to get plane resources");
            return;
        }
        defer { drmModeFreePlaneResources(plane_res); };

#define DO(Type, Name, Res) \
        for (decltype(Res->count_ ## Name) i = 0; i < Res->count_ ## Name; ++i) { \
            Name.emplace_back(drmModeGet ## Type(drm_fd, Res->Name[i])); \
        }

        DO(Connector, connectors, mode_res)
        DO(Encoder, encoders, mode_res)
        DO(Crtc, crtcs, mode_res)
        DO(Plane, planes, plane_res)

#undef DO
    }

#define DO(Type, Name) \
    drmMode##Type* find_##Name(u32 id) \
    { \
        for (auto* c : Name##s) if (c->Name##_id == id) return c;\
        return nullptr; \
    }

    DO(Connector, connector)
    DO(Encoder, encoder)
    DO(Crtc, crtc)
    DO(Plane, plane)

#undef DO

    ~drm_resources()
    {
        for (auto* c : connectors) drmModeFreeConnector(c);
        for (auto* e : encoders) drmModeFreeEncoder(e);
        for (auto* c : crtcs) drmModeFreeCrtc(c);
        for (auto* p : planes) drmModeFreePlane(p);
    }
};

struct drm_property_map
{
    using prop_deleter = decltype([](drmModeObjectProperties* v) { drmModeFreeObjectProperties(v); });
    using prop_ptr = std::unique_ptr<drmModeObjectProperties, prop_deleter>;
    prop_ptr props;

    ankerl::unordered_dense::map<std::string_view, drmModePropertyRes*> properties;

    drm_property_map() = default;

    drm_property_map(wroc_direct_backend* backend, u32 object_id, u32 object_type)
        : props(prop_ptr(drmModeObjectGetProperties(backend->drm_fd.get(), object_id, object_type)))
    {
        for (u32 i = 0; i < props->count_props; ++i) {
            auto* prop = drmModeGetProperty(backend->drm_fd.get(), props->props[i]);
            properties[prop->name] = prop;
        }
    }

    drm_property_map(const drm_property_map& other)
    {
        for (auto[name, old_prop] : other.properties) {
            auto* new_prop =  drmModeGetProperty(wroc_get_direct_backend()->drm_fd.get(), old_prop->prop_id);
            properties[new_prop->name] = new_prop;
        }
    }

    drm_property_map& operator=(const drm_property_map& other)
    {
        if (this != &other) {
            this->~drm_property_map();
            new(this) drm_property_map(other);
        }
        return *this;
    };

    drm_property_map(drm_property_map&& other)
        : properties(std::move(other.properties))
    {}

    drm_property_map& operator=(drm_property_map&& other)
    {
        if (this != &other) {
            this->~drm_property_map();
            new(this) drm_property_map(std::move(other));
        }
        return *this;
    }

    ~drm_property_map()
    {
        for (auto[_, prop] : properties) drmModeFreeProperty(prop);
    }

    u32 get_prop_id(std::string_view prop_name)
    {
        return properties.at(prop_name)->prop_id;
    }

    u64 get_prop_value(std::string_view prop_name)
    {
        auto id = get_prop_id(prop_name);
        for (u32 i = 0; i < props->count_props; ++i) {
            if (props->props[i] == id) {
                return props->prop_values[i];
            }
        }
        core_assert_fail("Failed to find property");
    }

    int get_enum_value(std::string_view prop_name, std::string_view enum_name)
    {
        auto* prop = properties.at(prop_name);

        core_assert(prop->flags & DRM_MODE_PROP_ENUM);
        for (int e = 0; e < prop->count_enums; ++e) {
            if (enum_name == prop->enums[e].name) {
                return prop->enums[e].value;
            }
        }

        log_error("Failed to find enum value: {}.{}", prop_name, enum_name);
        core_debugkill();
    }
};

// -----------------------------------------------------------------------------

static
gpu_format_set parse_plane_formats(wroc_direct_backend* backend, drm_resources* resources, drmModePlane* plane)
{
    drm_property_map props{backend, plane->plane_id, DRM_MODE_OBJECT_PLANE};
    auto blob_id = props.get_prop_value("IN_FORMATS");
    if (!blob_id) {
        log_error("Plane has no IN_FORMATS property");
        return {};
    }

    auto blob = drmModeGetPropertyBlob(backend->drm_fd.get(), blob_id);
    defer { drmModeFreePropertyBlob(blob); };

    auto* header = static_cast<drm_format_modifier_blob*>(blob->data);

    auto formats = core_byte_offset_pointer<gpu_drm_format>(blob->data, header->formats_offset);
    auto modifiers = core_byte_offset_pointer<drm_format_modifier>(blob->data, header->modifiers_offset);

    gpu_format_set set;
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

// -----------------------------------------------------------------------------

#define WROC_DRM_EXPERIMENTAL_BROKEN_TEARING_SUPPORT 0

struct wroc_drm_output_state
{
    u32 primary_plane_id;
    u32 crtc_id;
    u32 connector_id;

    drm_property_map plane_prop;
    drm_property_map crtc_prop;

    ref<gpu_semaphore> last_release_semaphore = {};
    u64 last_release_point;

    gpu_format_set format_set;

#if WROC_DRM_EXPERIMENTAL_BROKEN_TEARING_SUPPORT
    ref<gpu_semaphore> pending_release_semaphore = {};
    u64 pending_release_point;
#endif

    std::chrono::steady_clock::time_point last_commit_time = {};
};

// -----------------------------------------------------------------------------

wroc_drm_output::~wroc_drm_output()
{
    delete state;
}

static
void add_output(wroc_direct_backend* backend, drm_resources* resources, drmModeConnector* connector)
{
    // TODO: In the interest of fast prototyping, we will initially just re-use the existing
    //       configuration that we find, which should be that of the current TTY that we are
    //       being launched from.
    //
    //       In the future, we will want to handle reconfiguration of all DRM resources.

    // Find encoder for this connector

    if (connector->encoder_id == 0) {
        log_warn("Connector {} has no encoder", connector->connector_id);
        return;
    }
    auto encoder = resources->find_encoder(connector->encoder_id);
    core_assert(encoder);

    // Find CRTC currently used by this connector

    if (encoder->crtc_id == 0) {
        log_warn("Connector {} has no active CRTC", connector->connector_id);
        return;
    }
    auto* crtc = resources->find_crtc(encoder->crtc_id);
    core_assert(crtc);

    // Ensure the CRTC is active

    if (crtc->buffer_id == 0) {
        log_warn("Connector {} not active", connector->connector_id);
        return;
    }

    // Find plane currently used by CRTC

    drmModePlane* plane = nullptr;
    for (auto[i, p] : resources->planes | std::views::enumerate) {
        if (p->crtc_id == crtc->crtc_id && p->fb_id == crtc->buffer_id) {
            plane = p;
        }
    }
    core_assert(plane);

    // Compute refresh rate

    u64 refresh = ((crtc->mode.clock * 1000000ul / crtc->mode.htotal) + (crtc->mode.vtotal / 2)) / crtc->mode.vtotal;

    log_warn("Creating output");
    log_warn("  crtc: {}", crtc->crtc_id);
    log_warn("  conn: {}", connector->connector_id);
    log_warn("  plane: {}", plane->plane_id);
    log_warn("  refresh: {} mHz", refresh);
    log_warn("  extent: ({}, {})", crtc->width, crtc->height);

    auto output = core_create<wroc_drm_output>();
    output->state = new wroc_drm_output_state {
        .primary_plane_id = plane->plane_id,
        .crtc_id = crtc->crtc_id,
        .connector_id = connector->connector_id,

        .plane_prop = { backend, plane->plane_id, DRM_MODE_OBJECT_PLANE },
        .crtc_prop = { backend, crtc->crtc_id, DRM_MODE_OBJECT_CRTC },

        .format_set = parse_plane_formats(backend, resources, plane),
    };

    output->size = { crtc->width, crtc->height };

    output->desc.modes = {
        {
            .size = output->size,
            .refresh = refresh / 1000.0,
        }
    };

    output->desc.physical_size_mm = {};
    output->desc.model = "Unknown";
    output->desc.make = "Unknown";
    output->desc.name = std::format("DRM-{}", (void*)output.get());
    output->desc.description = std::format("DRM output {}", (void*)output.get());

    backend->outputs.emplace_back(output);
}

void wroc_backend_start_drm(wroc_direct_backend* backend)
{
    for (auto& output : backend->outputs) {
        wroc_post_event(wroc_output_event {
            .type = wroc_event_type::output_added,
            .output = output.get(),
        });

        output->frame_available = true;
        wroc_post_event(wroc_output_event {
            .type = wroc_event_type::output_frame,
            .output = output.get(),
        });
    }
}

static
void on_page_flip(int fd, u32 sequence, u32 tv_sec, u32 tv_usec, u32 crtc_id, void* data);

static
void drm_handle_event(wroc_direct_backend* backend, int fd, flags<core_fd_event_bit> events)
{
    drmEventContext handlers {
        .version = 3,
        .page_flip_handler2 = on_page_flip,
    };
    drmHandleEvent(fd, &handlers);
}

void wroc_backend_init_drm(wroc_direct_backend* backend)
{
    int drm_fd = [&] {
        auto* device = server->gpu->drm.device;
        if (!(device->available_nodes & (1 << DRM_NODE_PRIMARY))) return -1;
        auto* opened = wroc_open_restricted(backend, server->gpu->drm.device->nodes[DRM_NODE_PRIMARY]);
        if (!opened) return -1;
        return opened->fd;
    }();

    if (drm_fd < 0) {
        log_error("Failed to open DRM primary node for render device!");
        core_debugkill();
    }

    backend->drm_fd = core_fd_adopt(drm_fd);

    core_fd_add_listener(backend->drm_fd.get(), server->event_loop.get(), core_fd_event_bit::readable,
        [backend](int fd, flags<core_fd_event_bit> events) {
            drm_handle_event(backend, fd, events);
        });

    drm_magic_t magic;
    log_debug("Getting magic");
    unix_check<drmGetMagic>(drm_fd, &magic);
    log_debug("Authenticating magic");
    unix_check<drmAuthMagic>(drm_fd, magic);

    log_debug("Setting universal planes capability");
    unix_check<drmSetClientCap>(drm_fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
    log_debug("Setting atomic capability");
    unix_check<drmSetClientCap>(drm_fd, DRM_CLIENT_CAP_ATOMIC, 1);

    u64 cap = 0;

    log_debug("Checking for framebuffer modifier support");
    core_assert(unix_check<drmGetCap>(drm_fd, DRM_CAP_ADDFB2_MODIFIERS, &cap).ok() && cap);

    log_debug("Checking for monotonic timestamp support");
    core_assert(unix_check<drmGetCap>(drm_fd, DRM_CAP_TIMESTAMP_MONOTONIC, &cap).ok() && cap);

    drm_resources res(drm_fd);

    for (auto* connector : res.connectors) {
        add_output(backend, &res, connector);
    }

    // Intersect format sets for all outputs

    std::vector<const gpu_format_set*> format_sets;
    for (auto& output : backend->outputs) {
        format_sets.emplace_back(&output->state->format_set);
    }
    backend->format_set = gpu_intersect_format_sets(format_sets);
}

// -----------------------------------------------------------------------------

static
void on_page_flip(int fd, u32 sequence, u32 tv_sec, u32 tv_usec, u32 crtc_id, void* data)
{
    auto* output = static_cast<wroc_drm_output*>(data);

    auto time = core_steady_clock_from_timespec<CLOCK_MONOTONIC>(timespec {
        .tv_sec = tv_sec,
        .tv_nsec = tv_usec * 1000,
    });

#if WROC_DRM_EXPERIMENTAL_BROKEN_TEARING_SUPPORT
    if (output->state->pending_release_semaphore) {
        gpu_semaphore_signal_value(output->state->pending_release_semaphore.get(), output->state->pending_release_point);
        output->state->pending_release_semaphore = nullptr;
    }
#endif

    wroc_post_event(wroc_output_event {
        .type = wroc_event_type::output_commit,
        .timestamp = time,
        .output = output,
        .commit {
            .id = output->last_commit_id,
            .start = output->state->last_commit_time,
        },
    });

    output->frame_available = true;
    wroc_post_event(wroc_output_event {
        .type = wroc_event_type::output_frame,
        .timestamp = time,
        .output = output,
    });
}

static
u32 get_image_fb2(wroc_direct_backend* backend, gpu_image* image)
{
    std::optional<u32> found = std::nullopt;
    std::erase_if(backend->buffer_cache, [&](const auto& entry) {
        if (!entry.image) {
            drmCloseBufferHandle(backend->drm_fd.get(), entry.fb2_handle);
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
        unix_check<drmPrimeFDToHandle>(backend->drm_fd.get(), dma_params.planes[i].fd.get(), &handles[i]);
        log_warn("  plane[{}] prime fd {} -> GEM handle {}", i, dma_params.planes[i].fd.get(), handles[i]);
        pitches[i] = dma_params.planes[i].stride;
        offsets[i] = dma_params.planes[i].offset;
        modifiers[i] = dma_params.modifier;
    }

    // Import

    u32 fb2_handle = 0;
    unix_check<drmModeAddFB2WithModifiers>(backend->drm_fd.get(),
        size.x, size.y,
        format->drm, handles, pitches, offsets, modifiers,
        &fb2_handle, DRM_MODE_FB_MODIFIERS);

    // Close GEM handles

    std::flat_set<u32> unique_handles;
    unique_handles.insert_range(handles);
    for (auto handle : unique_handles) drmCloseBufferHandle(backend->drm_fd.get(), handle);

    log_warn("  FB2 id: {}", fb2_handle);

    return backend->buffer_cache.emplace_back(image, fb2_handle).fb2_handle;
}

wroc_output_commit_id wroc_drm_output::commit(
    gpu_image* image,
    gpu_syncpoint acquire,
    gpu_syncpoint release,
    flags<wroc_output_commit_flag> in_flags)
{
    core_assert(frame_available);
    frame_available = false;

    auto backend = wroc_get_direct_backend();

    auto fb2_handle = get_image_fb2(backend, image);

    auto req = drmModeAtomicAlloc();
    defer { drmModeAtomicFree(req); };

    auto plane_set = [&](std::string_view name, u64 value) {
        drmModeAtomicAddProperty(req, state->primary_plane_id, state->plane_prop.get_prop_id(name), value);
    };

    auto in_fence = gpu_semaphore_export_syncfile(acquire.semaphore, acquire.value);
    defer { close(in_fence); };

    plane_set("FB_ID", fb2_handle);
    plane_set("IN_FENCE_FD", in_fence);
    plane_set("SRC_X", 0);
    plane_set("SRC_Y", 0);
    plane_set("SRC_W", image->extent().x << 16);
    plane_set("SRC_H", image->extent().y << 16);
    plane_set("CRTC_X", 0);
    plane_set("CRTC_Y", 0);
    plane_set("CRTC_W", size.x);
    plane_set("CRTC_H", size.y);

    auto flags = DRM_MODE_ATOMIC_NONBLOCK | DRM_MODE_PAGE_FLIP_EVENT;

    int out_fence = -1;
    defer { close(out_fence); };
    if (state->last_release_semaphore) {
#if WROC_DRM_EXPERIMENTAL_BROKEN_TEARING_SUPPORT
        // For some reason some drivers don't seem to support OUT_FENCE_PTR when using async page flips.
        // Additionally, it seems that we are required to send at least one non-async flip first, hence
        // handling this only when `last_release_semaphore` is set.
        if (!(in_flags.contains(wroc_output_commit_flags::vsync))) {
            flags |= DRM_MODE_PAGE_FLIP_ASYNC;
            state->pending_release_semaphore = state->last_release_semaphore;
            state->pending_release_point = state->last_release_point;
        } else
#endif
        drmModeAtomicAddProperty(req, state->crtc_id, state->crtc_prop.get_prop_id("OUT_FENCE_PTR"), u64(&out_fence));
    }

    drmModeAtomicAddProperty(req, state->crtc_id, state->crtc_prop.get_prop_id("VRR_ENABLED"), true);

    if (unix_check<drmModeAtomicCommit>(backend->drm_fd.get(), req, flags, this).err()) {
        // TODO: Configuration rollback
        gpu_semaphore_import_syncfile(release.semaphore, in_fence, release.value);
        frame_available = true;
        return {};
    }

    if (state->last_release_semaphore) {
        gpu_semaphore_import_syncfile(state->last_release_semaphore.get(), out_fence, state->last_release_point);
    }

    state->last_commit_time = std::chrono::steady_clock::now();
    state->last_release_semaphore = release.semaphore;
    state->last_release_point = release.value;

    return ++last_commit_id;
}
