#include "protocol.hpp"

const u32 wroc_wl_seat_version = 10;

static
void wroc_wl_seat_get_keyboard(wl_client* client, wl_resource* resource, u32 id)
{
    auto* seat = wroc_get_userdata<wroc_seat>(resource);
    auto* new_resource = wroc_resource_create(client, &wl_keyboard_interface, wl_resource_get_version(resource), id);
    seat->keyboard->resources.emplace_back(new_resource);
    wroc_resource_set_implementation(new_resource, &wroc_wl_keyboard_impl, seat->keyboard.get());

    wroc_seat_keyboard_send_configuration(seat->keyboard.get(), client, new_resource);
}

static
void wroc_wl_seat_get_pointer(wl_client* client, wl_resource* resource, u32 id)
{
    auto* seat = wroc_get_userdata<wroc_seat>(resource);
    auto* new_resource = wroc_resource_create(client, &wl_pointer_interface, wl_resource_get_version(resource), id);
    seat->pointer->resources.emplace_back(new_resource);
    wroc_resource_set_implementation(new_resource, &wroc_wl_pointer_impl, seat->pointer.get());
}

const struct wl_seat_interface wroc_wl_seat_impl = {
    .get_pointer  = wroc_wl_seat_get_pointer,
    .get_keyboard = wroc_wl_seat_get_keyboard,
    WROC_STUB(get_touch),
    .release      = wroc_simple_resource_destroy_callback,
};

const struct wl_keyboard_interface wroc_wl_keyboard_impl = {
    .release = wroc_simple_resource_destroy_callback,
};

static
void wroc_wl_pointer_set_cursor(wl_client* client, wl_resource* resource, u32 serial, wl_resource* wl_surface, i32 x, i32 y)
{
    auto* surface = wroc_get_userdata<wroc_surface>(wl_surface);

    wroc_cursor_set(server->cursor.get(), client, surface, {x, y});
}

const struct wl_pointer_interface wroc_wl_pointer_impl = {
    .set_cursor = wroc_wl_pointer_set_cursor,
    .release    = wroc_simple_resource_destroy_callback,
};

void wroc_wl_seat_bind_global(wl_client* client, void* data, u32 version, u32 id)
{
    auto* seat = static_cast<wroc_seat*>(data);
    auto* new_resource = wroc_resource_create(client, &wl_seat_interface, version, id);
    seat->resources.emplace_back(new_resource);
    wroc_resource_set_implementation(new_resource, &wroc_wl_seat_impl, seat);
    if (version >= WL_SEAT_NAME_SINCE_VERSION) {
        wroc_send(wl_seat_send_name, new_resource, seat->name.c_str());
    }
    u32 caps = WL_SEAT_CAPABILITY_KEYBOARD | WL_SEAT_CAPABILITY_POINTER;
    wroc_send(wl_seat_send_capabilities, new_resource, caps);
};

void wroc_seat_init()
{
    server->seat = core::create<wroc_seat>();
    server->seat->name = "seat-0";

    wroc_seat_init_keyboard(server->seat.get());
    wroc_seat_init_pointer(server->seat.get());

    server->seat->pointer->keyboard = core::create<wroc_keyboard>();
    server->seat->keyboard->attach(server->seat->pointer->keyboard.get());
}
