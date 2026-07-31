#include "pipewire.hpp"

#include <core/timer.hpp>

struct ShellScreenCastSession;

struct ShellScreenCastPortal : ShellPlugin
{
    Shell* shell;

    sd_bus_slot* slot;
    std::unordered_map<std::string, Ref<ShellScreenCastSession>> sessions;

    Ref<ShellPwContext> context;

    Ref<GpuImagePool> pool;

    ~ShellScreenCastPortal()
    {
        sd_bus_slot_unref(slot);
    }
};

struct ShellScreenCastSession
{
    ShellScreenCastPortal* portal;

    sd_bus_slot* slot;
    sd_bus_track* track;

    Ref<ShellPwStream> stream;
    rect2f32 viewport;

    Listener<void()> frame_listener;
    Listener<void()> output_layout_listener;

    Listener<void(pw_stream_state)> stream_state_listener;
    Ref<Timer> timer;
    sd_bus_message* pending_start_message;

    Ref<GpuImage> last_image;

    ~ShellScreenCastSession()
    {
        log_error("SESSION DESTROYED");
        sd_bus_slot_unref(slot);
        sd_bus_track_unref(track);
    }
};

static
void configure_frame_callback(ShellScreenCastSession* session);

// -----------------------------------------------------------------------------

static constexpr u32 PORTAL_RESPONSE_SUCCESS = 0u;
static constexpr u32 PORTAL_RESPONSE_FAILURE = 1u;

static
auto session_close(sd_bus_message* m, void* data, sd_bus_error* error) -> int
{
    const char* path = sd_bus_message_get_path(m);

    log_debug("ScreenCast :: Session.Close on {}", path);

    auto* portal = static_cast<ShellScreenCastPortal*>(data);

    if (auto it = portal->sessions.find(path); it != portal->sessions.end()) {
        unix_check<sd_bus_emit_signal>(portal->shell->dbus, path, "org.freedesktop.impl.portal.Session", "Closed", "");
        portal->sessions.erase(it);
    }

    return unix_check<sd_bus_reply_method_return>(m, "").value;
}

static
auto session_disconnected(sd_bus_track *track, void *userdata) -> int
{
    auto* session = static_cast<ShellScreenCastSession*>(userdata);
    auto* portal = session->portal;

    log_warn("ScreenCast :: Session disconnected unexpectedly");

    std::erase_if(portal->sessions, [&](const auto& e) { return e.second.get() == session; });
    return 0;
}

static
auto reply_simple(sd_bus_message* m, u32 code) -> int
{
    sd_bus_message* reply = nullptr;
    unix_check<sd_bus_message_new_method_return>(m, &reply);
    defer { sd_bus_message_unref(reply); };
    unix_check<sd_bus_message_append>(reply, "ua{sv}", code, 0);
    return unix_check<sd_bus_send>(nullptr, reply, nullptr).value;
}

static
auto method_create_session(sd_bus_message* m, void* data, sd_bus_error* error) -> int
{
    auto* portal = static_cast<ShellScreenCastPortal*>(data);

    const char* handle;
    const char* session_handle;
    const char* app_id;
    unix_check<sd_bus_message_read>(m, "oos", &handle, &session_handle, &app_id);
    unix_check<sd_bus_message_skip>(m, "a{sv}");

    log_debug("ScreenCast :: CreateSession app_id='{}' session='{}' (currently open sessions: {})", app_id, session_handle, portal->sessions.size());

    sd_bus_slot* slot = nullptr;

    static constexpr sd_bus_vtable vtable[] = {
        SD_BUS_VTABLE_START(0),
        SD_BUS_METHOD("Close", "", "", session_close, SD_BUS_VTABLE_UNPRIVILEGED),
        SD_BUS_SIGNAL("Closed", "", 0),
        SD_BUS_VTABLE_END,
    };

    auto res = unix_check<sd_bus_add_object_vtable>(portal->shell->dbus, &slot, session_handle, "org.freedesktop.impl.portal.Session", vtable, portal);
    if (res.err()) {
        log_error("ScreenCast :: CreateSession failed to register session object path: [{}]", session_handle);
        return reply_simple(m, PORTAL_RESPONSE_FAILURE);
    }

    auto& session = portal->sessions[session_handle];
    session = ref_create<ShellScreenCastSession>();
    session->portal = portal;

    session->slot = slot;
    unix_check<sd_bus_track_new>(portal->shell->dbus, &session->track, session_disconnected, &session);
    unix_check<sd_bus_track_add_sender>(session->track, m);

    // session->stream = shell_pw_stream_create(portal->context.get(), {3840, 2160});
    // session->stream = shell_pw_stream_create(portal->context.get(), {1280, 720});
    // session->viewport = {{}, {1313, 720}, xywh};
    // session->viewport = {{}, {1280 - 1, 720}, xywh};
    // session->stream = shell_pw_stream_create(portal->context.get(), vec_cast<u32>(session->viewport.extent));

    auto* wm = portal->shell->wm.get();
    session->output_layout_listener = wm_get_signals(wm).output_layout.listen([session = session.get()] {
        configure_frame_callback(session);
    });

    configure_frame_callback(session.get());

    return reply_simple(m, PORTAL_RESPONSE_SUCCESS);
}

// -----------------------------------------------------------------------------

static
auto method_select_sources(sd_bus_message* m, void* data, sd_bus_error* error) -> int
{
    const char* handle;
    const char* session_handle;
    const char* app_id;;
    unix_check<sd_bus_message_read>(m, "oos", &handle, &session_handle, &app_id);
    unix_check<sd_bus_message_skip>(m, "a{sv}");

    log_debug("ScreenCast :: SelectSources session='{}'", session_handle);

    auto* portal = static_cast<ShellScreenCastPortal*>(data);
    auto sessions_iter = portal->sessions.find(session_handle);
    if (sessions_iter == portal->sessions.end()) {
        log_error("ScreenCast :: No session found for handle: [{}]", session_handle);
        return reply_simple(m, PORTAL_RESPONSE_FAILURE);
    }
    auto session = sessions_iter->second.get();

    wm_begin_selection(portal->shell->wm.get(), seat_get_pointer(wm_get_seat(portal->shell->wm.get())),
        [session = Weak(session), m = sd_bus_message_ref(m)](rect2f32 viewport) {
            defer { sd_bus_message_unref(m); };

            if (!session || !viewport.extent) {
                session->viewport = {};
                log_warn("ScreenCast selection was empty or session was closed");
                reply_simple(m, PORTAL_RESPONSE_FAILURE);
                return;
            }

            session->viewport = {viewport.origin, vec_round(viewport.extent), xywh};
            log_warn("ScreenCast selection made: {}", session->viewport);
            reply_simple(m, PORTAL_RESPONSE_SUCCESS);
        });

    return 1;
    // session->viewport = {{}, {1313, 720}, xywh};
    // log_warn("ScreenCast selection made: {}", session->viewport);
    // reply_simple(m, PORTAL_RESPONSE_SUCCESS);
    // return 0;
}

// -----------------------------------------------------------------------------

static
auto method_start(sd_bus_message* m, void* data, sd_bus_error* error) -> int
{
    auto* portal = static_cast<ShellScreenCastPortal*>(data);

    const char* handle;
    const char* session_handle;
    const char* app_id;
    const char* parent_window;
    unix_check<sd_bus_message_read>(m, "ooss", &handle, &session_handle, &app_id, &parent_window);
    unix_check<sd_bus_message_skip>(m, "a{sv}");

    auto sessions_iter = portal->sessions.find(session_handle);
    if (sessions_iter == portal->sessions.end()) {
        log_error("ScreenCast :: No session found for handle: [{}]", session_handle);
        return reply_simple(m, PORTAL_RESPONSE_FAILURE);
    }

    auto session = sessions_iter->second.get();
    if (!session->viewport.extent) {
        log_error("ScreenCast :: Region size is zero", session_handle);
        return reply_simple(m, PORTAL_RESPONSE_FAILURE);
    }

    debug_assert(vec_round(session->viewport.extent) == session->viewport.extent);
    auto extent = vec_cast<u32>(session->viewport.extent);

    log_debug("CREATING STREAM WITH EXTENT: {}", extent);

    // // session->stream = shell_pw_stream_create(portal->context.get(), {1920, 1079});
    session->stream = shell_pw_stream_create(portal->context.get(), extent);

    session->pending_start_message = sd_bus_message_ref(m);
    session->stream_state_listener = session->stream->signals.state_changed.listen([session](pw_stream_state state) {
        if (state == PW_STREAM_STATE_PAUSED && session->pending_start_message) {
    // if (!session->timer) session->timer = timer_create(portal->shell->exec);
    // timer_enqueue(session->timer.get(), std::chrono::steady_clock::now() + 1s, [session] {
    //     {
            defer {
                sd_bus_message_unref(session->pending_start_message);
                session->pending_start_message = nullptr;
            };

            auto* portal = session->portal;

            auto node_id = pw_stream_get_node_id(session->stream->stream);

            log_debug("ScreenCast :: Stream [{}] finished connecting", node_id);

            for (auto* output : wm_get_outputs(portal->shell->wm.get())) {
                wm_output_damage(output);
            }

            sd_bus_message* reply = nullptr;
            unix_check<sd_bus_message_new_method_return>(session->pending_start_message, &reply);
            defer { sd_bus_message_unref(reply); };

            unix_check<sd_bus_message_append>(reply, "u", PORTAL_RESPONSE_SUCCESS);

            // results: { "streams": [ (node_id, {}) ] }
            unix_check<sd_bus_message_open_container>(reply, 'a', "{sv}");
                unix_check<sd_bus_message_open_container>(reply, 'e', "sv");
                    unix_check<sd_bus_message_append>(reply, "s", "streams");
                    unix_check<sd_bus_message_open_container>(reply, 'v', "a(ua{sv})");
                        unix_check<sd_bus_message_open_container>(reply, 'a', "(ua{sv})");
                            unix_check<sd_bus_message_open_container>(reply, 'r', "ua{sv}");
                                unix_check<sd_bus_message_append>(reply, "u", node_id);
                                unix_check<sd_bus_message_open_container>(reply, 'a', "{sv}");
                                unix_check<sd_bus_message_close_container>(reply);
                            unix_check<sd_bus_message_close_container>(reply);
                        unix_check<sd_bus_message_close_container>(reply);
                    unix_check<sd_bus_message_close_container>(reply);
                unix_check<sd_bus_message_close_container>(reply);
            unix_check<sd_bus_message_close_container>(reply);

            unix_check<sd_bus_send>(nullptr, reply, nullptr);
        }
    });

    return 1;

    // log_debug("ScreenCast :: Start session='{}' -> node id {}", session_handle, pw_stream_get_node_id(session->stream->stream));

    // auto node_id = pw_stream_get_node_id(session->stream->stream);

    // log_debug("ScreenCast :: Stream [{}] finished connecting", node_id);

    // for (auto* output : wm_get_outputs(portal->shell->wm.get())) {
    //     wm_output_damage(output);
    // }

    // sd_bus_message* reply = nullptr;
    // unix_check<sd_bus_message_new_method_return>(m, &reply);
    // defer { sd_bus_message_unref(reply); };

    // unix_check<sd_bus_message_append>(reply, "u", PORTAL_RESPONSE_SUCCESS);

    // // results: { "streams": [ (node_id, {}) ] }
    // unix_check<sd_bus_message_open_container>(reply, 'a', "{sv}");
    //     unix_check<sd_bus_message_open_container>(reply, 'e', "sv");
    //         unix_check<sd_bus_message_append>(reply, "s", "streams");
    //         unix_check<sd_bus_message_open_container>(reply, 'v', "a(ua{sv})");
    //             unix_check<sd_bus_message_open_container>(reply, 'a', "(ua{sv})");
    //                 unix_check<sd_bus_message_open_container>(reply, 'r', "ua{sv}");
    //                     unix_check<sd_bus_message_append>(reply, "u", node_id);
    //                     unix_check<sd_bus_message_open_container>(reply, 'a', "{sv}");
    //                     unix_check<sd_bus_message_close_container>(reply);
    //                 unix_check<sd_bus_message_close_container>(reply);
    //             unix_check<sd_bus_message_close_container>(reply);
    //         unix_check<sd_bus_message_close_container>(reply);
    //     unix_check<sd_bus_message_close_container>(reply);
    // unix_check<sd_bus_message_close_container>(reply);

    // unix_check<sd_bus_send>(nullptr, reply, nullptr);

    // return 0;
}

// -----------------------------------------------------------------------------

static
auto prop_source_types(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void*, sd_bus_error*) -> int
{
    return unix_check<sd_bus_message_append>(reply, "u", /* MONITOR */ 1u).value;
}

static
auto prop_cursor_modes(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void*, sd_bus_error*) -> int
{
    return unix_check<sd_bus_message_append>(reply, "u", /* HIDDEN */ 1u | /* EMBEDDED */ 2u).value;
}

auto prop_version(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void*, sd_bus_error*) -> int
{
    return unix_check<sd_bus_message_append>(reply, "u", 4u).value;
}

// -----------------------------------------------------------------------------

void shell_init_screencast(Shell* shell)
{
    auto portal = ref_create<ShellScreenCastPortal>();
    shell->plugins.emplace_back(portal.get());

    portal->context = shell_pw_context_create(shell->exec, shell->gpu.get());

    portal->shell = shell;

    portal->pool = gpu_image_pool_create(shell->gpu.get());

    static constexpr sd_bus_vtable vtable[] = {
        SD_BUS_VTABLE_START(0),
        SD_BUS_METHOD_WITH_ARGS("CreateSession",
            SD_BUS_ARGS("o", handle, "o", session_handle, "s", app_id, "a{sv}", options),
            SD_BUS_RESULT("u", response, "a{sv}", results),
            method_create_session, SD_BUS_VTABLE_UNPRIVILEGED),
        SD_BUS_METHOD_WITH_ARGS("SelectSources",
            SD_BUS_ARGS("o", handle, "o", session_handle, "s", app_id, "a{sv}", options),
            SD_BUS_RESULT("u", response, "a{sv}", results),
            method_select_sources, SD_BUS_VTABLE_UNPRIVILEGED),
        SD_BUS_METHOD_WITH_ARGS("Start",
            SD_BUS_ARGS("o", handle, "o", session_handle, "s", app_id, "s", parent_window, "a{sv}", options),
            SD_BUS_RESULT("u", response, "a{sv}", results),
            method_start, SD_BUS_VTABLE_UNPRIVILEGED),
        SD_BUS_PROPERTY("AvailableSourceTypes", "u", prop_source_types, 0, SD_BUS_VTABLE_PROPERTY_CONST),
        SD_BUS_PROPERTY("AvailableCursorModes", "u", prop_cursor_modes, 0, SD_BUS_VTABLE_PROPERTY_CONST),
        SD_BUS_PROPERTY("version",              "u", prop_version,      0, SD_BUS_VTABLE_PROPERTY_CONST),
        SD_BUS_VTABLE_END,
    };

    unix_check<sd_bus_add_object_vtable>(shell->dbus, &portal->slot,
        "/org/freedesktop/portal/desktop", "org.freedesktop.impl.portal.ScreenCast",
        vtable, portal.get());
}

// -----------------------------------------------------------------------------

#define SCREENCAST_NOISY_STREAM 0

static
void frame(ShellScreenCastSession* session)
{
    auto* portal = session->portal;
    auto* stream = session->stream.get();
    if (!stream) return;

    auto* buf = shell_pw_stream_dequeue(stream);
    if (!buf) return;

    auto* gpu = stream->ctx->gpu;
    auto* wm = portal->shell->wm.get();

#if SCREENCAST_NOISY_STREAM
    {
        static auto last = std::chrono::steady_clock::now();
        static usz frames = 0;
        auto now = std::chrono::steady_clock::now();
        frames++;
        auto delta = now - last;
        if (delta > 500ms) {
            auto delta_s = std::chrono::duration_cast<std::chrono::duration<f64>>(delta).count();
            log_trace("PIPEWIRE :: Frametime {} ({:.2f}/s) {}", FmtDuration{(now - last) / frames}, f64(frames) / delta_s, buf->dmabuf ? "DMA" : "SHM");
            last = now;
            frames = 0;
        }
    }
#endif

    if (buf->dmabuf) {
        scene_render(wm_get_scene_renderer(wm), {
            .root = wm_get_scene(wm),
            .target = buf->dmabuf.get(),
            .viewport = session->viewport,
        });

        gpu_wait(gpu_flush(gpu), [stream = Weak(stream), buf](u64) {
            if (!stream) return;
            shell_pw_stream_enqueue(stream.get(), buf);
        });
    } else {
        auto image = portal->pool->acquire({
            .extent = stream->extent,
            .format = stream->format,
            .usage = GpuImageUsage::storage,
        });
        session->last_image = image;
        u32 row_stride = gpu_image_compute_packed_stride(stream->format, stream->extent.x);
        auto staging = gpu_buffer_create(gpu, row_stride * stream->extent.y, GpuBufferFlag::host);

        scene_render(wm_get_scene_renderer(wm), {
            .root = wm_get_scene(wm),
            .target = image.get(),
            .viewport = session->viewport,
        });
        gpu_copy_image_to_buffer(staging.get(), image.get());

        gpu_wait(gpu_flush(gpu), [stream = Weak(stream), buf, staging](u64) {
            if (!stream) return;

            u32 row_stride = gpu_image_compute_packed_stride(stream->format, stream->extent.x);
            std::memcpy(buf->mapped, staging->host_address, row_stride * stream->extent.y);

            shell_pw_stream_enqueue(stream.get(), buf);
        });
    }
}

static
void configure_frame_callback(ShellScreenCastSession* session)
{
    for (auto[i, output] : wm_get_outputs(session->portal->shell->wm.get()) | std::views::enumerate) {
        auto viewport = wm_output_get_viewport(output);
        if (rect_contains(viewport, vec2f32{})) {
            log_info("PIPEWIRE :: Syncing stream to output[{}]: {}", i, viewport);
            session->frame_listener = wm_output_get_signals(output).frame.listen([session] {
                frame(session);
            });
            break;
        }
    }
}
