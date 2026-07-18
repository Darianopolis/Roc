#include "internal.hpp"

// -----------------------------------------------------------------------------

static
auto get_keymap_file(xkb_keymap* keymap) -> WayKeymap
{
    auto string = xkb_keymap_get_as_string(keymap, XKB_KEYMAP_FORMAT_TEXT_V1);
    defer { free(string); };
    u32 size = num_cast<u32>(strlen(string)) + 1;

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
    auto& kb_info = seat_keyboard_get_info(seat_get_keyboard(seat->seat));
    seat->keymap = get_keymap_file(kb_info.keymap);
}

// -----------------------------------------------------------------------------

void way_seat_get_keyboard(wl_client* wl_client, wl_resource* resource, u32 id)
{
    auto* client_seat = way_get_userdata<WayClientSeat>(resource);
    auto* seat = client_seat->seat;

    auto* kb = way_resource_create_refcounted(wl_keyboard, wl_client, resource, id, client_seat);
    client_seat->keyboards.emplace_back(kb);

    way_send<wl_keyboard_send_keymap>(kb,
        WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1,
        seat->keymap.fd.get(), seat->keymap.size);

    auto& kb_info = seat_keyboard_get_info(seat_get_keyboard(client_seat->seat->seat));

    if (wl_resource_get_version(kb) >= WL_KEYBOARD_REPEAT_INFO_SINCE_VERSION) {
        way_send<wl_keyboard_send_repeat_info>(kb, kb_info.rate, kb_info.delay);
    }
}

WAY_INTERFACE(wl_keyboard) = {
    .release = way_simple_destroy,
};

// -----------------------------------------------------------------------------

void way_seat_on_keyboard_leave(WayClientSeat* client_seat, SeatEvent* event)
{
    auto* seat = client_seat->seat;
    auto* server = seat->server;

    auto* surface = seat->focus.keyboard.get();
    seat->focus.keyboard = nullptr;
    if (!surface || !surface->resource) return;

    auto serial = way_next_serial(server);
    for (auto* keyboard : client_seat->keyboards) {
        way_send<wl_keyboard_send_leave>(keyboard, serial.value, surface->resource);

        // Modifiers are tracked independently of keyboard enter/leave events
        way_send<wl_keyboard_send_modifiers>(keyboard, serial.value, 0u, 0u, 0u, 0u);
    }
}

static
void send_modifiers(WayClientSeat* client_seat)
{
    auto* seat = client_seat->seat;
    auto* server = seat->server;

    auto serial = way_next_serial(server);
    auto kb = seat_keyboard_get_info(seat_get_keyboard(seat->seat));

    for (auto* resource : client_seat->keyboards) {
        way_send<wl_keyboard_send_modifiers>(resource, serial.value,
            xkb_state_serialize_mods(  kb.state, XKB_STATE_MODS_DEPRESSED),
            xkb_state_serialize_mods(  kb.state, XKB_STATE_MODS_LATCHED),
            xkb_state_serialize_mods(  kb.state, XKB_STATE_MODS_LOCKED),
            xkb_state_serialize_layout(kb.state, XKB_STATE_LAYOUT_EFFECTIVE));
    }
}

void way_seat_on_keyboard_enter(WayClientSeat* client_seat, SeatEvent* event)
{
    auto* seat = client_seat->seat;
    auto* server = seat->server;

    auto* surface = way_find_surface_for_focus(client_seat->client, event->keyboard.focus);
    seat->focus.keyboard = surface;

    if (surface->toplevel->window) {
        wm_window_raise(surface->toplevel->window.get());
    }

    auto serial = way_next_serial(server);

    if (surface->resource) {
        auto pressed = way_from_span<const u32>(seat_keyboard_get_pressed(event->keyboard.keyboard));
        for (auto* resource : client_seat->keyboards) {
            way_send<wl_keyboard_send_enter>(resource, serial.value, surface->resource, &pressed);
        }
    } else {
        log_error("Keyboard enter failed: wl_surface is destroyed for {}", (void*)surface);
    }

    send_modifiers(client_seat);

    way_data_offer_selection(client_seat);
}

// -----------------------------------------------------------------------------

void way_seat_on_key(WayClientSeat* client_seat, SeatEvent* event)
{
    auto* seat = client_seat->seat;
    auto* server = seat->server;

    auto serial = way_next_serial(server);
    auto elapsed = way_get_elapsed(server);
    auto time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    auto key = event->keyboard.key;
    auto state = key.pressed ? WL_KEYBOARD_KEY_STATE_PRESSED : WL_KEYBOARD_KEY_STATE_RELEASED;

    for (auto* resource : client_seat->keyboards) {
        way_send<wl_keyboard_send_key>(resource, serial.value, num_cast<u32>(time_ms), key.code, state);
    }
}

void way_seat_on_modifier(WayClientSeat* client_seat, SeatEvent* event)
{
    send_modifiers(client_seat);
}
