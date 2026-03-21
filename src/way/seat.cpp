#include "internal.hpp"

static
auto get_keymap_file(xkb_keymap* keymap) -> way_keymap
{
    auto string = xkb_keymap_get_as_string(keymap, XKB_KEYMAP_FORMAT_TEXT_V1);
    defer { free(string); };
    u32 size = strlen(string) + 1;

    auto fd = core_fd(unix_check<memfd_create>(PROGRAM_NAME "-keymap", MFD_ALLOW_SEALING | MFD_CLOEXEC).value);
    unix_check<ftruncate>(fd.get(), size);

    auto mapped = unix_check<mmap>(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd.get(), 0).value;
    memcpy(mapped, string, size);
    munmap(mapped, size);

    // Seal file to prevent further writes
    unix_check<fcntl>(fd.get(), F_ADD_SEALS, F_SEAL_WRITE | F_SEAL_SHRINK | F_SEAL_GROW);

    return { .fd = std::move(fd), .size = size };
}

static
auto get_keyboard(way_server* server)
{
    // TODO: HACK HACK HACK - Handle keyboard per seat dynamically
    auto seats = scene_get_seats(server->scene);
    core_assert(seats.size() == 1);
    return scene_seat_get_keyboard(seats.front());
}

void way_seat_init(way_server* server)
{
    auto& kb_info = scene_keyboard_get_info(get_keyboard(server));

    server->keyboard.keymap = get_keymap_file(kb_info.keymap);
}

// -----------------------------------------------------------------------------

static
void get_keyboard(wl_client* wl_client, wl_resource* resource, u32 id)
{
    auto* client = way_get_userdata<way_client>(resource);
    auto* server = client->server;

    auto* kb = way_resource_create_unsafe(wl_keyboard, wl_client, resource, id, client);
    client->keyboards.emplace_back(kb);

    way_send(server, wl_keyboard_send_keymap, kb,
        WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1,
        server->keyboard.keymap.fd.get(), server->keyboard.keymap.size);

    auto& kb_info = scene_keyboard_get_info(get_keyboard(server));

    if (wl_resource_get_version(kb) >= WL_KEYBOARD_REPEAT_INFO_SINCE_VERSION) {
        way_send(server, wl_keyboard_send_repeat_info, kb, kb_info.rate, kb_info.delay);
    }
}

WAY_INTERFACE(wl_keyboard) = {
    .release = way_simple_destroy,
};

// -----------------------------------------------------------------------------

static
void get_pointer(wl_client* wl_client, wl_resource* resource, u32 id)
{
    auto* client = way_get_userdata<way_client>(resource);

    client->pointers.emplace_back(way_resource_create_unsafe(wl_pointer, wl_client, resource, id, client));
}

static
void set_cursor(wl_client* wl_client, wl_resource* resource, u32 serial, wl_resource* wl_surface, int hot_x, int hot_y)
{
    auto* client = way_get_userdata<way_client>(resource);
    auto* surface = wl_surface ? way_get_userdata<way_surface>(wl_surface) : nullptr;

    if (surface) {
        scene_tree_set_translation(surface->scene.tree.get(), {-hot_x, -hot_y});

        if (surface->role != way_surface_role::cursor) {
            surface->role = way_surface_role::cursor;
            scene_node_unparent(surface->scene.input_region.get());
        }
    }

    if (client->server->pointer.scene) {
        scene_pointer_set_cursor(client->server->pointer.scene, surface ? surface->scene.tree.get() : nullptr);
    }
}

WAY_INTERFACE(wl_pointer) = {
    .set_cursor = set_cursor,
    .release = way_simple_destroy,
};

// -----------------------------------------------------------------------------

WAY_INTERFACE(wl_seat) = {
    .get_pointer = get_pointer,
    .get_keyboard = get_keyboard,
    WAY_STUB(get_touch),
    .release = way_simple_destroy,
};

WAY_BIND_GLOBAL(wl_seat, bind)
{
    auto* client = way_client_from(bind.server, bind.client);

    auto* resource = way_resource_create_unsafe(wl_seat, bind.client, bind.version, bind.id, client);

    way_send(bind.server, wl_seat_send_capabilities, resource, WL_SEAT_CAPABILITY_KEYBOARD | WL_SEAT_CAPABILITY_POINTER);

    if (bind.version >= WL_SEAT_NAME_SINCE_VERSION) {
        way_send(bind.server, wl_seat_send_name, resource, "seat0");
    }
}

// -----------------------------------------------------------------------------

static
auto find_surface(way_client* client, scene_input_region* region) -> way_surface*
{
    if (!region) return nullptr;
    if (region->client != client->scene.get()) return nullptr;
    for (auto* surface : client->surfaces) {
        if (surface->scene.input_region.get() == region) return surface;
    }
    return nullptr;
}

static
auto elapsed_ms(way_server* server) -> u32
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(way_get_elapsed(server)).count();
}

// -----------------------------------------------------------------------------

static
void keyboard_leave(way_client* client)
{
    auto* server = client->server;
    auto* surface = server->focus.keyboard.get();
    if (!surface) return;

    server->keyboard.scene = nullptr;
    u32 serial = way_next_serial(server);

    if (surface->wl_surface) {
        for (auto* resource : client->keyboards) {
            way_send(server, wl_keyboard_send_leave, resource, serial, surface->wl_surface);
        }
    }

    core_assert(server->focus.keyboard.get() == surface, "Keyboard left surface that did not have focus");
    server->focus.keyboard = nullptr;
}

void way_seat_on_keyboard_leave(way_client* client, scene_event* event)
{
    keyboard_leave(client);
}

static
auto find_root_toplevel(way_surface* surface) -> way_surface*
{
    if (surface->role == way_surface_role::xdg_toplevel) return surface;
    core_assert(surface->parent);
    return find_root_toplevel(surface->parent.get());
}

void way_seat_on_keyboard_enter(way_client* client, scene_event* event)
{
    auto* server = client->server;

    auto* surface = find_surface(client, event->keyboard.focus.region);

    // xdg_popup and wl_subsurface cannot have keyboard focus
    surface = find_root_toplevel(surface);
    if (surface == server->focus.keyboard.get()) return;

    // Leave previous window
    keyboard_leave(client);

    if (surface->toplevel.window) {
        scene_window_raise(surface->toplevel.window.get());
    }

    server->keyboard.scene = event->keyboard.keyboard;

    u32 serial = way_next_serial(server);

    if (surface->wl_surface) {
        auto pressed = way_to_wl_array<const u32>(scene_keyboard_get_pressed(server->keyboard.scene));
        for (auto* resource : client->keyboards) {
            way_send(server, wl_keyboard_send_enter, resource, serial, surface->wl_surface, &pressed);
        }
    } else {
        log_error("Keyboard enter failed: wl_surface is destroyed for {}", (void*)surface);
    }

    server->focus.keyboard = surface;

    way_data_offer_selection(client);
}

// -----------------------------------------------------------------------------

static
auto get_fixed_pos(way_surface* surface, scene_pointer* pointer) -> std::pair<wl_fixed_t, wl_fixed_t>
{
    auto local = scene_pointer_get_position(pointer) - scene_tree_get_position(surface->scene.tree.get());

    return {wl_fixed_from_double(local.x), wl_fixed_from_double(local.y)};
}

static
void pointer_leave_surface(way_client* client, way_surface* surface, u32 serial)
{
    auto* server = client->server;

    if (!surface->wl_surface) return;
    for (auto* resource : client->pointers) {
        way_send(server, wl_pointer_send_leave, resource, serial, surface->wl_surface);
    }
}

void way_seat_on_pointer_leave(way_client* client, scene_event* event)
{
    auto* server = client->server;

    if (auto* surface = server->focus.pointer.get()) {
        pointer_leave_surface(client, surface, way_next_serial(server));
    }

    server->focus.pointer = nullptr;
    server->pointer.scene = nullptr;
}

void way_seat_on_pointer_enter(way_client* client, scene_event* event)
{
    auto* server = client->server;

    server->pointer.scene = event->pointer.pointer;

    u32 serial = way_next_serial(server);

    auto* old_surface = server->focus.pointer.get();
    auto* new_surface = find_surface(client, event->pointer.focus.region);

    if (old_surface && new_surface && old_surface != new_surface) {
        pointer_leave_surface(client, old_surface, serial);
    }

    server->focus.pointer = new_surface;

    if (!new_surface->wl_surface) return;
    auto[x, y] = get_fixed_pos(new_surface, server->pointer.scene);
    for (auto* resource : client->pointers) {
        way_send(server, wl_pointer_send_enter, resource, serial, new_surface->wl_surface, x, y);
    }
}

// -----------------------------------------------------------------------------

void way_seat_on_key(way_client* client, scene_event* event)
{
    auto* server = client->server;
    u32 serial = way_next_serial(server);
    u32 time = elapsed_ms(server);
    auto key = event->keyboard.key;
    auto state = key.pressed ? WL_KEYBOARD_KEY_STATE_PRESSED : WL_KEYBOARD_KEY_STATE_RELEASED;

    for (auto* resource : client->keyboards) {
        way_send(server, wl_keyboard_send_key, resource, serial, time, key.code, state);
    }
}

void way_seat_on_modifier(way_client* client, scene_event* event)
{
    auto* server = client->server;
    u32 serial = way_next_serial(server);
    auto kb = scene_keyboard_get_info(server->keyboard.scene);

    for (auto* resource : client->keyboards) {
        way_send(server, wl_keyboard_send_modifiers, resource, serial,
            xkb_state_serialize_mods(  kb.state, XKB_STATE_MODS_DEPRESSED),
            xkb_state_serialize_mods(  kb.state, XKB_STATE_MODS_LATCHED),
            xkb_state_serialize_mods(  kb.state, XKB_STATE_MODS_LOCKED),
            xkb_state_serialize_layout(kb.state, XKB_STATE_LAYOUT_EFFECTIVE));
    }
}

// -----------------------------------------------------------------------------

static
void pointer_frame(way_server* server, wl_resource* resource)
{
    if (wl_resource_get_version(resource) >= WL_POINTER_FRAME_SINCE_VERSION) {
        way_send(server, wl_pointer_send_frame, resource);
    }
}

void way_seat_on_motion(way_client* client, scene_event* event)
{
    auto* server = client->server;
    auto* surface = server->focus.pointer.get();
    if (!surface) return;

    u32 time = elapsed_ms(server);
    auto[x, y] = get_fixed_pos(surface, server->pointer.scene);

    for (auto* resource : client->pointers) {
        way_send(server, wl_pointer_send_motion, resource, time, x, y);
        pointer_frame(server, resource);
    }
}

void way_seat_on_button(way_client* client, scene_event* event)
{
    auto* server = client->server;
    u32 serial = way_next_serial(server);
    u32 time = elapsed_ms(server);
    auto button = event->pointer.button;
    auto state = button.pressed ? WL_POINTER_BUTTON_STATE_PRESSED : WL_POINTER_BUTTON_STATE_RELEASED;

    for (auto* resource : client->pointers) {
        way_send(server, wl_pointer_send_button, resource, serial, time, button.code, state);
        pointer_frame(server, resource);
    }
}

void way_seat_on_scroll(way_client* client, scene_event* event)
{
    auto* server = client->server;
    u32 time = elapsed_ms(server);
    auto delta = event->pointer.scroll.delta;

    static constexpr f32 axis_pixel_rate = 15.f;

    for (auto* resource : client->pointers) {
        if (delta.x) {
            way_send(server, wl_pointer_send_axis, resource, time,
                WL_POINTER_AXIS_HORIZONTAL_SCROLL, wl_fixed_from_double(delta.x * axis_pixel_rate));
        }
        if (delta.y) {
            way_send(server, wl_pointer_send_axis, resource, time,
                WL_POINTER_AXIS_VERTICAL_SCROLL, wl_fixed_from_double(delta.y * axis_pixel_rate));
        }
        pointer_frame(server, resource);
    }
}
