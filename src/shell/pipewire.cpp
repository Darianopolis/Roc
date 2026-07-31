#include "pipewire.hpp"

#include <core/log.hpp>

static
void check(int res)
{
    debug_assert(res >= 0, "PIPEWIRE FAIL: {} ({})", strerror(-res), res);
}

template<typename T>
static
T* check(T* t)
{
    debug_assert(t, "PIPEWIRE FAIL (nullptr)");
    return t;
}

auto to_spa_format(GpuFormat format) -> spa_video_format
{
	switch (format->drm) {
        case DRM_FORMAT_ARGB8888:    return SPA_VIDEO_FORMAT_BGRA;
        case DRM_FORMAT_XRGB8888:    return SPA_VIDEO_FORMAT_BGRx;
        case DRM_FORMAT_RGBA8888:    return SPA_VIDEO_FORMAT_ABGR;
        case DRM_FORMAT_RGBX8888:    return SPA_VIDEO_FORMAT_xBGR;
        case DRM_FORMAT_ABGR8888:    return SPA_VIDEO_FORMAT_RGBA;
        case DRM_FORMAT_XBGR8888:    return SPA_VIDEO_FORMAT_RGBx;
        case DRM_FORMAT_BGRA8888:    return SPA_VIDEO_FORMAT_ARGB;
        case DRM_FORMAT_BGRX8888:    return SPA_VIDEO_FORMAT_xRGB;
        case DRM_FORMAT_NV12:        return SPA_VIDEO_FORMAT_NV12;
        case DRM_FORMAT_XRGB2101010: return SPA_VIDEO_FORMAT_xRGB_210LE;
        case DRM_FORMAT_XBGR2101010: return SPA_VIDEO_FORMAT_xBGR_210LE;
        case DRM_FORMAT_RGBX1010102: return SPA_VIDEO_FORMAT_RGBx_102LE;
        case DRM_FORMAT_BGRX1010102: return SPA_VIDEO_FORMAT_BGRx_102LE;
        case DRM_FORMAT_ARGB2101010: return SPA_VIDEO_FORMAT_ARGB_210LE;
        case DRM_FORMAT_ABGR2101010: return SPA_VIDEO_FORMAT_ABGR_210LE;
        case DRM_FORMAT_RGBA1010102: return SPA_VIDEO_FORMAT_RGBA_102LE;
        case DRM_FORMAT_BGRA1010102: return SPA_VIDEO_FORMAT_BGRA_102LE;
        case DRM_FORMAT_BGR888:      return SPA_VIDEO_FORMAT_RGB;
        case DRM_FORMAT_RGB888:      return SPA_VIDEO_FORMAT_BGR;
        default:                     return SPA_VIDEO_FORMAT_UNKNOWN;
	}
}

auto from_spa_format(spa_video_format spa) -> GpuFormat
{
	switch (spa) {
        case SPA_VIDEO_FORMAT_BGRA:       return gpu_format_from_drm(DRM_FORMAT_ARGB8888);
        case SPA_VIDEO_FORMAT_BGRx:       return gpu_format_from_drm(DRM_FORMAT_XRGB8888);
        case SPA_VIDEO_FORMAT_ABGR:       return gpu_format_from_drm(DRM_FORMAT_RGBA8888);
        case SPA_VIDEO_FORMAT_xBGR:       return gpu_format_from_drm(DRM_FORMAT_RGBX8888);
        case SPA_VIDEO_FORMAT_RGBA:       return gpu_format_from_drm(DRM_FORMAT_ABGR8888);
        case SPA_VIDEO_FORMAT_RGBx:       return gpu_format_from_drm(DRM_FORMAT_XBGR8888);
        case SPA_VIDEO_FORMAT_ARGB:       return gpu_format_from_drm(DRM_FORMAT_BGRA8888);
        case SPA_VIDEO_FORMAT_xRGB:       return gpu_format_from_drm(DRM_FORMAT_BGRX8888);
        case SPA_VIDEO_FORMAT_NV12:       return gpu_format_from_drm(DRM_FORMAT_NV12);
        case SPA_VIDEO_FORMAT_xRGB_210LE: return gpu_format_from_drm(DRM_FORMAT_XRGB2101010);
        case SPA_VIDEO_FORMAT_xBGR_210LE: return gpu_format_from_drm(DRM_FORMAT_XBGR2101010);
        case SPA_VIDEO_FORMAT_RGBx_102LE: return gpu_format_from_drm(DRM_FORMAT_RGBX1010102);
        case SPA_VIDEO_FORMAT_BGRx_102LE: return gpu_format_from_drm(DRM_FORMAT_BGRX1010102);
        case SPA_VIDEO_FORMAT_ARGB_210LE: return gpu_format_from_drm(DRM_FORMAT_ARGB2101010);
        case SPA_VIDEO_FORMAT_ABGR_210LE: return gpu_format_from_drm(DRM_FORMAT_ABGR2101010);
        case SPA_VIDEO_FORMAT_RGBA_102LE: return gpu_format_from_drm(DRM_FORMAT_RGBA1010102);
        case SPA_VIDEO_FORMAT_BGRA_102LE: return gpu_format_from_drm(DRM_FORMAT_BGRA1010102);
        case SPA_VIDEO_FORMAT_RGB:        return gpu_format_from_drm(DRM_FORMAT_BGR888);
        case SPA_VIDEO_FORMAT_BGR:        return gpu_format_from_drm(DRM_FORMAT_RGB888);
        default:                          return gpu_format_from_drm(DRM_FORMAT_INVALID);
	}
}

// -----------------------------------------------------------------------------

static
auto build_format(spa_pod_builder* b, GpuFormat format, vec2u32 extent, u32 framerate, std::span<const u64> modifiers) -> spa_pod*
{
    spa_pod_frame f;
    check(spa_pod_builder_push_object(b, &f, SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat));

    check(spa_pod_builder_add(b,  SPA_FORMAT_mediaType,    SPA_POD_Id(SPA_MEDIA_TYPE_video), nullptr));
    check(spa_pod_builder_add(b,  SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw), nullptr));

    // Format

    auto spa_format = to_spa_format(format);

    // TODO: Show non-alpha versions for unmodified formats
    check(spa_pod_builder_add(b,  SPA_FORMAT_VIDEO_format, SPA_POD_Id(spa_format), nullptr));

    // Modifiers

    if (!modifiers.empty()) {
        if (modifiers.size() == 1) {
            spa_pod_builder_prop(b, SPA_FORMAT_VIDEO_modifier, SPA_POD_PROP_FLAG_MANDATORY);
            spa_pod_builder_long(b, std::bit_cast<i64>(modifiers[0]));
        } else {
            check(spa_pod_builder_prop(b, SPA_FORMAT_VIDEO_modifier, SPA_POD_PROP_FLAG_MANDATORY | SPA_POD_PROP_FLAG_DONT_FIXATE));

            spa_pod_frame cf;
            check(spa_pod_builder_push_choice(b, &cf, SPA_CHOICE_Enum, 0));
            check(spa_pod_builder_long(b, std::bit_cast<i64>(modifiers[0])));
            for (auto mod : modifiers) {
                check(spa_pod_builder_long(b, std::bit_cast<i64>(mod)));
            }
            check(spa_pod_builder_pop(b, &cf));
        }
    }

    // Size

    check(spa_pod_builder_add(b, SPA_FORMAT_VIDEO_size, SPA_POD_Rectangle(ptr_to(SPA_RECTANGLE(extent.x, extent.y))), nullptr));

    // Framerate

    check(spa_pod_builder_add(b, SPA_FORMAT_VIDEO_framerate, SPA_POD_Fraction(ptr_to(SPA_FRACTION(0, 1))), nullptr));

    if (framerate > 0) {
        check(spa_pod_builder_add(b, SPA_FORMAT_VIDEO_maxFramerate,
            SPA_POD_CHOICE_RANGE_Fraction(
                ptr_to(SPA_FRACTION(framerate, 1)),
                ptr_to(SPA_FRACTION(1, 1)),
                ptr_to(SPA_FRACTION(framerate, 1))),
            nullptr));
    }

    return static_cast<spa_pod*>(spa_pod_builder_pop(b, &f));
}

static
auto build_buffer(spa_pod_builder* b, u32 blocks, u32 size, u32 stride, u32 datatype) -> spa_pod*
{
    spa_pod_frame f;
    spa_pod_builder_push_object(b, &f, SPA_TYPE_OBJECT_ParamBuffers, SPA_PARAM_Buffers);
    spa_pod_builder_add(b, SPA_PARAM_BUFFERS_buffers, SPA_POD_CHOICE_RANGE_Int(2, 2, 32), nullptr); // number of buffers
    spa_pod_builder_add(b, SPA_PARAM_BUFFERS_blocks, SPA_POD_Int(blocks), nullptr);
    if (size > 0) {
        spa_pod_builder_add(b, SPA_PARAM_BUFFERS_size, SPA_POD_Int(size), nullptr);
    }
    if (stride > 0) {
        spa_pod_builder_add(b, SPA_PARAM_BUFFERS_stride, SPA_POD_Int(stride), nullptr);
    }
    spa_pod_builder_add(b, SPA_PARAM_BUFFERS_align, SPA_POD_Int(16), nullptr);
    spa_pod_builder_add(b, SPA_PARAM_BUFFERS_dataType, SPA_POD_CHOICE_FLAGS_Int(datatype), nullptr);
    return static_cast<spa_pod*>(spa_pod_builder_pop(b, &f));
}

// -----------------------------------------------------------------------------

static
void on_state_changed(void* data, pw_stream_state old, pw_stream_state state, const char* error)
{
    auto* stream = static_cast<ShellPwStream*>(data);
    stream->state = state;
    if (error) {
        log_error("PIPEWIRE stream {} state changed: {} -> {} (error: {})", int(pw_stream_get_node_id(stream->stream)),  old, state, error);
    } else {
        log_debug("PIPEWIRE stream {} state changed: {} -> {}", int(pw_stream_get_node_id(stream->stream)),  old, state);
    }

    stream->signals.state_changed(state);
    // if (state == PW_STREAM_STATE_STREAMING) {
    //     for (auto* output : wm_get_outputs(stream->wm)) {
    //         wm_output_damage(output);
    //     }
    // }
}

static constexpr auto DAMAGE_REGION_COUNT = 16;

static
void on_param_changed(void* data, u32 id, const spa_pod* param)
{
    auto* stream = static_cast<ShellPwStream*>(data);
    log_error("PIPEWIRE param changed (id: {} ({}), param: {})", spa_param_type(id), id, (void*)param);

    if (!param || id != SPA_PARAM_Format) {
        return;
    }

    spa_pod_dynamic_builder builder;
    spa_pod_dynamic_builder_init(&builder, nullptr, 0, 65'536);
    defer { spa_pod_dynamic_builder_clean(&builder); };

    spa_video_info_raw fmt_info;
    spa_format_video_raw_parse(param, &fmt_info);
    if (fmt_info.max_framerate.denom > 0) {
        f64 framerate = f64(fmt_info.max_framerate.num) / fmt_info.max_framerate.denom;
        log_warn("  framerate: {}", framerate);
    } else {
        log_warn("  framerate: unset");
    }

    log_warn("  MODIFIER: {}", gpu_get_modifier_name(fmt_info.modifier));

    u32 blocks;
    u32 data_type;

    const spa_pod_prop* prop_modifier;
    if ((prop_modifier = spa_pod_find_prop(param, nullptr, SPA_FORMAT_VIDEO_modifier)) != nullptr) {
        log_warn("  attempting to negotiate DMABUF format");

		if ((prop_modifier->flags & SPA_POD_PROP_FLAG_DONT_FIXATE) > 0) {
            log_warn("  client has returned list of modifiers, making a temporary DMABUF to chose one to fixate");

            auto* pod_modifier = &prop_modifier->value;
            u32 n_modifiers = num_cast<u32>(SPA_POD_CHOICE_N_VALUES(pod_modifier)) - 1u;
            u64* modifiers = static_cast<u64*>(SPA_POD_CHOICE_VALUES(pod_modifier)) + 1;
            GpuFormatModifierSet mod_set;
            for (u32 i = 0; i < n_modifiers; ++i) {
                log_debug("    - {}", gpu_get_modifier_name(modifiers[i]));
                mod_set.insert(modifiers[i]);
            }

            auto format = from_spa_format(fmt_info.format);
            auto image = gpu_image_create(stream->ctx->gpu, GpuImageCreateInfo{
                .extent = stream->extent,
                .format = format,
                .usage = GpuImageUsage::storage,
                .modifiers = &mod_set,
            });

            auto modifier = image->base()->modifier;

            log_warn("  fixating on modifier: {}", gpu_get_modifier_name(modifier));

            std::vector<const spa_pod*> params;
            params.emplace_back(build_format(&builder.b, format, stream->extent, 0, {{modifier}}));
            check(pw_stream_update_params(stream->stream, params.data(), u32(params.size())));
            return;
        }

        blocks = 1; // TODO: multi-plane
        data_type = 1 << SPA_DATA_DmaBuf;
        stream->modifier = fmt_info.modifier;
    } else {
        log_warn("  attempting to negotiate wl_shm format");
        blocks = 1;
        data_type = 1 << SPA_DATA_MemFd;
        stream->modifier = DRM_FORMAT_MOD_LINEAR;
    }

    stream->format = from_spa_format(fmt_info.format);

    std::vector<const spa_pod*> params;
    params.emplace_back(build_buffer(&builder.b, blocks, 0, 0, data_type));

    // params.emplace_back((spa_pod*)spa_pod_builder_add_object(&builder.b,
    //     SPA_TYPE_OBJECT_ParamMeta, SPA_PARAM_Meta,
    //     SPA_PARAM_META_type, SPA_POD_Id(SPA_META_Header),
    //     SPA_PARAM_META_size, SPA_POD_Int(sizeof(spa_meta_header))));

    // params.emplace_back((spa_pod*)spa_pod_builder_add_object(&builder.b,
    //     SPA_TYPE_OBJECT_ParamMeta, SPA_PARAM_Meta,
    //     SPA_PARAM_META_type, SPA_POD_Id(SPA_META_VideoTransform),
    //     SPA_PARAM_META_size, SPA_POD_Int(sizeof(struct spa_meta_videotransform))));

    // params.emplace_back((spa_pod*)spa_pod_builder_add_object(&builder.b,
	// 	SPA_TYPE_OBJECT_ParamMeta, SPA_PARAM_Meta,
	// 	SPA_PARAM_META_type, SPA_POD_Id(SPA_META_VideoDamage),
	// 	SPA_PARAM_META_size, SPA_POD_CHOICE_RANGE_Int(
	// 		sizeof(struct spa_meta_region) * DAMAGE_REGION_COUNT,
	// 		sizeof(struct spa_meta_region) * 1,
	// 		sizeof(struct spa_meta_region) * DAMAGE_REGION_COUNT)));

    check(pw_stream_update_params(stream->stream, params.data(), u32(params.size())));
}

ShellPwBuffer::~ShellPwBuffer()
{
    if (mapped) {
        unix_check<munmap>(mapped, extent.y * stride);
    }
}

static
void on_add_buffer(void* data, pw_buffer* buffer)
{
    auto* stream = static_cast<ShellPwStream*>(data);
    log_error("PIPEWIRE add_buffer<{}> (num datas: {})", (void*)buffer, buffer->buffer->n_datas);

    u32 row_stride = gpu_image_compute_packed_stride(stream->format, stream->extent.x);
    u32 size = row_stride * stream->extent.y;

    auto buf = object_create_unsafe<ShellPwBuffer>();
    buffer->user_data = buf;
    buf->buffer = buffer;
    buf->extent = stream->extent;
    buf->format = stream->format;

    u32 flags = SPA_DATA_FLAG_READABLE;
    spa_data_type t;

    auto* datas = buffer->buffer->datas;
    if ((datas[0].type & (1u << SPA_DATA_MemFd)) > 0) {
        t = SPA_DATA_MemFd;
        flags |= SPA_DATA_FLAG_MAPPABLE;
        buf->fd = Fd(unix_check<memfd_create>(PROGRAM_NAME "-pipewire-memfd-buffer", MFD_CLOEXEC).value);
        unix_check<ftruncate>(buf->fd.get(), size);

        buf->mapped = static_cast<u8*>(unix_check<mmap>(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, buf->fd.get(), 0).value);
    } else {
        t = SPA_DATA_DmaBuf;
        buf->dmabuf = gpu_image_create(stream->ctx->gpu, {
            .extent = stream->extent,
            .format = stream->format,
            .usage = GpuImageUsage::storage,
            .modifiers = ptr_to(GpuFormatModifierSet{stream->modifier}),
        });
        buf->fd = gpu_image_export(buf->dmabuf.get()).planes[0].fd;

        auto exported = gpu_image_export(buf->dmabuf.get());
        row_stride = exported.planes[0].stride;
        size = row_stride * stream->extent.y;

        debug_assert(buf->dmabuf->base()->memory.count == 1, "TODO: Multi-plane support");
    }

    log_warn("CREATING BUFFER (fd: {}, fd, size: {})", buf->fd.get(), size);

    datas[0].type = t;
    datas[0].maxsize = size;
    datas[0].mapoffset = 0;
    datas[0].chunk->size = size;
    datas[0].chunk->stride = num_cast<i32>(row_stride);
    datas[0].chunk->offset = 0;
    datas[0].flags = flags;
    datas[0].fd = buf->fd.get();
    datas[0].data = nullptr;
}

static
void on_remove_buffer(void* data, pw_buffer* b)
{
    log_error("PIPEWIRE remove_buffer<{}>", (void*)b);

    object_unref(static_cast<ShellPwBuffer*>(b->user_data));
}

auto shell_pw_stream_dequeue(ShellPwStream* stream) -> ShellPwBuffer*
{
    if (stream->state != PW_STREAM_STATE_STREAMING) return nullptr;

    auto buffer = pw_stream_dequeue_buffer(stream->stream);
    if (!buffer) return nullptr;

    return static_cast<ShellPwBuffer*>(buffer->user_data);
}

void shell_pw_stream_enqueue(ShellPwStream* stream, ShellPwBuffer* buffer)
{
    pw_stream_queue_buffer(stream->stream, buffer->buffer);
}

static constexpr pw_stream_events stream_events {
    .version = PW_VERSION_STREAM_EVENTS,
    .state_changed = on_state_changed,
    .param_changed = on_param_changed,
    .add_buffer = on_add_buffer,
    .remove_buffer = on_remove_buffer,
};

// -----------------------------------------------------------------------------

ShellPwContext::~ShellPwContext()
{
    check(pw_core_disconnect(core));

    pw_context_destroy(context);

    fd_unlisten(exec, pw_loop_get_fd(loop));
	pw_loop_leave(loop);
    pw_loop_destroy(loop);

    pw_deinit();
}

ShellPwStream::~ShellPwStream()
{
	check(pw_stream_flush(stream, false));
	check(pw_stream_disconnect(stream));
    pw_stream_destroy(stream);
}

static constexpr pw_core_events core_events = {
    .version = PW_VERSION_CORE_EVENTS,
    .error = [](void*, u32 id, int seq, int res, const char* message) {
        log_error("PIPEWIRE core event error: {}", message);
    }
};

auto shell_pw_context_create(ExecContext* exec, Gpu* gpu) -> Ref<ShellPwContext>
{
    auto ctx = ref_create<ShellPwContext>();
    ctx->exec = exec;
    ctx->gpu = gpu;

    // PipeWire Loop

    pw_init(nullptr, nullptr);

    ctx->loop = check(pw_loop_new(nullptr));
    fd_listen(exec, pw_loop_get_fd(ctx->loop), FdEventBit::readable, [ctx = ctx.get()](fd_t fd, Flags<FdEventBit> events) {
        check(pw_loop_iterate(ctx->loop, 0));
    });

    // PipeWire Context / Core

    ctx->context = check(pw_context_new(ctx->loop, nullptr, 0));
    ctx->core = check(pw_context_connect(ctx->context, nullptr, 0));
    spa_zero(ctx->core_hook);
    pw_core_add_listener(ctx->core, &ctx->core_hook, &core_events, ctx.get());

	pw_loop_enter(ctx->loop);

    return ctx;
}

auto shell_pw_stream_create(ShellPwContext* ctx, vec2u32 extent) -> Ref<ShellPwStream>
{
    u32 id_a = std::random_device{}();
    u32 id_b = std::random_device{}();

    auto name = std::format(PROGRAM_NAME "-screencast-{:x}{:x}", id_a, id_b);
    auto desc = std::format(PROJECT_NAME " ScreenCast {:x}{:x}", id_a, id_b);

    // PipeWire Stream

    auto props = check(pw_properties_new(
        PW_KEY_MEDIA_CLASS,     "Video/Source",
        PW_KEY_MEDIA_TYPE,      "Video",
        PW_KEY_MEDIA_CATEGORY,  "Capture",
        PW_KEY_MEDIA_ROLE,      "Screen",
        PW_KEY_NODE_NAME,        name.c_str(),
        PW_KEY_NODE_DESCRIPTION, desc.c_str(),
        nullptr));

    auto stream = ref_create<ShellPwStream>();
    stream->ctx = ctx;

    stream->stream = check(pw_stream_new(ctx->core, name.c_str(), props));
    spa_zero(stream->stream_hook);
    pw_stream_add_listener(stream->stream, &stream->stream_hook, &stream_events, stream.get());

    stream->extent = extent;

    log_debug("STREAM EXTENT {}", extent);

    // NOTE: Fixed stream properties for now

    auto format = gpu_format_from_drm(DRM_FORMAT_XRGB8888);
    auto modifiers = gpu_get_format_properties(ctx->gpu, format, GpuImageUsage::storage)->mods;

    spa_pod_dynamic_builder builder;
    spa_pod_dynamic_builder_init(&builder, nullptr, 0, 65'536);
    defer { spa_pod_dynamic_builder_clean(&builder); };

    std::vector<const spa_pod*> params;
    params.emplace_back(build_format(&builder.b, format, stream->extent, 0, modifiers));
    // params.emplace_back(build_format(&builder.b, format, stream->extent, 0, {{DRM_FORMAT_MOD_LINEAR}}));
    // params.emplace_back(build_format(&builder.b, format, stream->extent, 0, {}));

    check(pw_stream_connect(stream->stream,
        PW_DIRECTION_OUTPUT,
        PW_ID_ANY,
        literal_cast<pw_stream_flags>(PW_STREAM_FLAG_ALLOC_BUFFERS | PW_STREAM_FLAG_DRIVER),
        params.data(), num_cast<u32>(params.size())));

    return stream;
}
