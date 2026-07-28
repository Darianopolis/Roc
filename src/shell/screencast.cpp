#include "pipewire.hpp"

struct ShellScreenCastPortal : ShellPlugin
{
    Shell* shell;

    sd_bus_slot* slot;
    std::unordered_map<std::string, sd_bus_slot*> sessions;

    ~ShellScreenCastPortal()
    {
        for (auto[_, session] : sessions) {
            sd_bus_slot_unref(session);
        }
        sd_bus_slot_unref(slot);
    }
};

auto session_close(sd_bus_message* m, void* data, sd_bus_error* error) -> int
{
    const char* path = sd_bus_message_get_path(m);

    log_debug("ScreenCast :: Session.Close on {}", path);

    auto* ctx = static_cast<ShellScreenCastPortal*>(data);

    if (auto it = ctx->sessions.find(path); it != ctx->sessions.end()) {
        unix_check<sd_bus_emit_signal>(ctx->shell->dbus, path, "org.freedesktop.impl.portal.Session", "Closed", "");
        sd_bus_slot_unref(it->second);
        ctx->sessions.erase(it);
    }

    return unix_check<sd_bus_reply_method_return>(m, "").value;
}

void export_session(ShellScreenCastPortal* ctx, const char* session_handle)
{
    sd_bus_slot* slot = nullptr;

    static constexpr sd_bus_vtable vtable[] = {
        SD_BUS_VTABLE_START(0),
        SD_BUS_METHOD("Close", "", "", session_close, SD_BUS_VTABLE_UNPRIVILEGED),
        SD_BUS_SIGNAL("Closed", "", 0),
        SD_BUS_VTABLE_END,
    };

    auto r = unix_check<sd_bus_add_object_vtable>(ctx->shell->dbus, &slot, session_handle, "org.freedesktop.impl.portal.Session", vtable, ctx);
    if (r.err()) return;

    ctx->sessions.emplace(session_handle, slot);
}

auto method_create_session(sd_bus_message* m, void* data, sd_bus_error* error) -> int
{
    auto* ctx = static_cast<ShellScreenCastPortal*>(data);

    const char* handle;
    const char* session_handle;
    const char* app_id;
    unix_check<sd_bus_message_read>(m, "oos", &handle, &session_handle, &app_id);
    unix_check<sd_bus_message_skip>(m, "a{sv}");

    log_debug("ScreenCast :: CreateSession app_id='{}' session='{}'", app_id, session_handle);

    export_session(ctx, session_handle);

    sd_bus_message* reply = nullptr;
    unix_check<sd_bus_message_new_method_return>(m, &reply);
    defer { sd_bus_message_unref(reply); };

    unix_check<sd_bus_message_append>(reply, "ua{sv}", /* PORTAL_RESPONSE_SUCCESS */ 0u, 0);

    return unix_check<sd_bus_send>(nullptr, reply, nullptr).value;
}

auto method_select_sources(sd_bus_message* m, void* data, sd_bus_error* error) -> int
{
    const char* handle;
    const char* session_handle;
    const char* app_id;;
    unix_check<sd_bus_message_read>(m, "oos", &handle, &session_handle, &app_id);
    unix_check<sd_bus_message_skip>(m, "a{sv}");

    log_debug("ScreenCast :: SelectSources session='{}'", session_handle);

    sd_bus_message* reply = nullptr;
    unix_check<sd_bus_message_new_method_return>(m, &reply);
    defer { sd_bus_message_unref(reply); };

    unix_check<sd_bus_message_append>(reply, "ua{sv}", /* PORTAL_RESPONSE_SUCCESS */ 0u, 0);

    return unix_check<sd_bus_send>(nullptr, reply, nullptr).value;
}

auto method_start(sd_bus_message* m, void* data, sd_bus_error* error) -> int
{
    auto* ctx = static_cast<ShellScreenCastPortal*>(data);

    const char* handle;
    const char* session_handle;
    const char* app_id;
    const char* parent_window;
    unix_check<sd_bus_message_read>(m, "ooss", &handle, &session_handle, &app_id, &parent_window);
    unix_check<sd_bus_message_skip>(m, "a{sv}");

    auto node_id = pw_stream_get_node_id(shell_pw_find_plugin(ctx->shell)->stream->stream);

    log_debug("ScreenCast :: Start session='{}' -> node id {}", session_handle, node_id);

    for (auto* output : wm_get_outputs(ctx->shell->wm.get())) {
        wm_output_damage(output);
    }

    sd_bus_message* reply = nullptr;
    unix_check<sd_bus_message_new_method_return>(m, &reply);
    defer { sd_bus_message_unref(reply); };

    unix_check<sd_bus_message_append>(reply, "u", /* PORTAL_RESPONSE_SUCCESS */ 0u);

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

    return unix_check<sd_bus_send>(nullptr, reply, nullptr).value;
}

auto prop_source_types(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void*, sd_bus_error*) -> int
{
    return unix_check<sd_bus_message_append>(reply, "u", /* MONITOR */ 1u).value;
}

auto prop_cursor_modes(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void*, sd_bus_error*) -> int
{
    return unix_check<sd_bus_message_append>(reply, "u", /* HIDDEN */ 1u | /* EMBEDDED */ 2u).value;
}

auto prop_version(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void*, sd_bus_error*) -> int
{
    return unix_check<sd_bus_message_append>(reply, "u", 4u).value;
}

void shell_init_screencast(Shell* shell)
{
    auto portal = ref_create<ShellScreenCastPortal>();
    shell->plugins.emplace_back(portal.get());

    portal->shell = shell;

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
