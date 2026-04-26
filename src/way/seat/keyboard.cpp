#include "internal.hpp"

// -----------------------------------------------------------------------------

static
auto get_keymap_file(xkb_keymap* keymap) -> WayKeymap
{
    auto string = xkb_keymap_get_as_string(keymap, XKB_KEYMAP_FORMAT_TEXT_V1);
    defer { free(string); };
    u32 size = strlen(string) + 1;

    auto fd = Fd(unix_check<memfd_create>(PROGRAM_NAME "-keymap", MFD_ALLOW_SEALING | MFD_CLOEXEC).value);
    unix_check<ftruncate>(fd.get(), size);

    auto mapped = unix_check<mmap>(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd.get(), 0).value;
    memcpy(mapped, string, size);
    munmap(mapped, size);

    // Seal file to prevent further writes
    unix_check<fcntl>(fd.get(), F_ADD_SEALS, F_SEAL_WRITE | F_SEAL_SHRINK | F_SEAL_GROW);

    return { .fd = std::move(fd), .size = size };
}

void way_seat_keyboard_init(WaySeat* seat)
{
    auto& kb_info = seat_keyboard_get_info(seat_get_keyboard(seat->scene));
    seat->keyboard.keymap = get_keymap_file(kb_info.keymap);
}

// -----------------------------------------------------------------------------

void way_seat_get_keyboard(wl_client* wl_client, wl_resource* resource, u32 id)
{
    auto* seat_client = way_get_userdata<WaySeatClient>(resource);
    auto* seat = seat_client->seat;

    auto* kb = way_resource_create_refcounted(wl_keyboard, wl_client, resource, id, seat_client);
    seat_client->keyboards.emplace_back(kb);

    way_send(wl_keyboard_send_keymap, kb,
        WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1,
        seat->keyboard.keymap.fd.get(), seat->keyboard.keymap.size);

    auto& kb_info = seat_keyboard_get_info(seat_get_keyboard(seat_client->seat->scene));

    if (wl_resource_get_version(kb) >= WL_KEYBOARD_REPEAT_INFO_SINCE_VERSION) {
        way_send(wl_keyboard_send_repeat_info, kb, kb_info.rate, kb_info.delay);
    }
}

WAY_INTERFACE(wl_keyboard) = {
    .release = way_simple_destroy,
};

// -----------------------------------------------------------------------------

static
void keyboard_leave(WaySeatClient* seat_client)
{
    auto* seat = seat_client->seat;
    auto* server = seat->server;
    auto* surface = seat->focus.keyboard.get();
    if (!surface) return;

    seat->keyboard.scene = nullptr;
    auto serial = way_next_serial(server);

    if (surface->resource) {
        for (auto* resource : seat_client->keyboards) {
            way_send(wl_keyboard_send_leave, resource, serial.value, surface->resource);
        }
    }

    debug_assert(seat->focus.keyboard.get() == surface, "Keyboard left surface that did not have focus");
    seat->focus.keyboard = nullptr;
}

void way_seat_on_keyboard_leave(WaySeatClient* seat_client, SeatEvent* event)
{
    keyboard_leave(seat_client);
}

static
auto find_root_toplevel(WaySurface* surface) -> WaySurface*
{
    if (surface->role == WaySurfaceRole::xdg_toplevel) return surface;
    debug_assert(surface->parent);
    return find_root_toplevel(surface->parent.get());
}

void way_seat_on_keyboard_enter(WaySeatClient* seat_client, SeatEvent* event)
{
    auto* seat = seat_client->seat;
    auto* server = seat->server;

    auto* surface = find_surface(seat_client->client, event->keyboard.focus);

    // xdg_popup and wl_subsurface cannot have keyboard focus
    surface = find_root_toplevel(surface);
    if (surface == seat->focus.keyboard.get()) return;

    // Leave previous window
    keyboard_leave(seat_client);

    if (surface->toplevel->window) {
        wm_window_raise(surface->toplevel->window.get());
    }

    seat->keyboard.scene = event->keyboard.keyboard;

    auto serial = way_next_serial(server);

    if (surface->resource) {
        auto pressed = way_to_wl_array<const u32>(seat_keyboard_get_pressed(seat->keyboard.scene));
        for (auto* resource : seat_client->keyboards) {
            way_send(wl_keyboard_send_enter, resource, serial.value, surface->resource, &pressed);
        }
    } else {
        log_error("Keyboard enter failed: wl_surface is destroyed for {}", (void*)surface);
    }

    seat->focus.keyboard = surface;

    way_data_offer_selection(seat_client);
}

// -----------------------------------------------------------------------------

void way_seat_on_key(WaySeatClient* seat_client, SeatEvent* event)
{
    auto* seat = seat_client->seat;
    auto* server = seat->server;

    auto serial = way_next_serial(server);
    auto elapsed = way_get_elapsed(server);
    u64 time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    auto key = event->keyboard.key;
    auto state = key.pressed ? WL_KEYBOARD_KEY_STATE_PRESSED : WL_KEYBOARD_KEY_STATE_RELEASED;

    for (auto* resource : seat_client->keyboards) {
        way_send(wl_keyboard_send_key, resource, serial.value, time_ms, key.code, state);
    }
}

void way_seat_on_modifier(WaySeatClient* seat_client, SeatEvent* event)
{
    auto* seat = seat_client->seat;
    auto* server = seat->server;

    auto serial = way_next_serial(server);
    auto kb = seat_keyboard_get_info(seat->keyboard.scene);

    for (auto* resource : seat_client->keyboards) {
        way_send(wl_keyboard_send_modifiers, resource, serial.value,
            xkb_state_serialize_mods(  kb.state, XKB_STATE_MODS_DEPRESSED),
            xkb_state_serialize_mods(  kb.state, XKB_STATE_MODS_LATCHED),
            xkb_state_serialize_mods(  kb.state, XKB_STATE_MODS_LOCKED),
            xkb_state_serialize_layout(kb.state, XKB_STATE_LAYOUT_EFFECTIVE));
    }
}
