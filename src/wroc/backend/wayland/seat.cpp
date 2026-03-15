#include "backend.hpp"

#include "wroc/util.hpp"

#include "wroc/event.hpp"

// -----------------------------------------------------------------------------

static
void wroc_backend_pointer_absolute(wroc_wayland_pointer* pointer, wl_fixed_t sx, wl_fixed_t sy)
{
    vec2f64 pos = {wl_fixed_to_double(sx), wl_fixed_to_double(sy)};
    pointer->absolute(pointer->current_output.get(), pos);
}

static
void wroc_listen_wl_pointer_enter(void* data, wl_pointer*, u32 serial, wl_surface* surface, wl_fixed_t sx, wl_fixed_t sy)
{
    log_info("pointer_enter");

    auto* pointer = static_cast<wroc_wayland_pointer*>(data);
    pointer->last_serial = serial;
    pointer->current_output = wroc_wayland_backend_find_output_for_surface(static_cast<wroc_wayland_backend*>(server->backend.get()), surface);

    wroc_backend_pointer_absolute(pointer, sx, sy);

    wl_pointer_set_cursor(pointer->wl_pointer, serial, nullptr, 0, 0);
}

static
void wroc_listen_wl_pointer_leave(void* data, wl_pointer*, u32 serial, wl_surface*)
{
    log_info("pointer_leave");

    auto* pointer = static_cast<wroc_wayland_pointer*>(data);
    pointer->last_serial = serial;

    pointer->leave();
}

static
void wroc_listen_wl_pointer_motion(void* data, wl_pointer*, u32 time, wl_fixed_t sx, wl_fixed_t sy)
{
    auto* pointer = static_cast<wroc_wayland_pointer*>(data);

    if (pointer->current_output->locked) return;
    wroc_backend_pointer_absolute(pointer, sx, sy);
}

static
void wroc_listen_wl_pointer_button(void* data, wl_pointer*, u32 serial, u32 time, u32 button, u32 state)
{
    auto* pointer = static_cast<wroc_wayland_pointer*>(data);
    pointer->last_serial = serial;

    switch (state) {
        break;case WL_POINTER_BUTTON_STATE_PRESSED:  pointer->press(button);
        break;case WL_POINTER_BUTTON_STATE_RELEASED: pointer->release(button);
    }
}

static
void wroc_listen_wl_pointer_frame(void* data, wl_pointer*)
{
    // TODO: Accumulate until frame
}

#define WROC_WAYLAND_BACKEND_NOISY_POINTER_AXIS 0

static
void wroc_listen_wl_pointer_axis_value120(void* data, wl_pointer*, u32 axis, i32 value120)
{
#if WROC_WAYLAND_BACKEND_NOISY_POINTER_AXIS
    log_debug("pointer_axis_value120(axis = {}, value = {})", core::to_string(wl_pointer_axis(axis)), value120);
#endif

    auto* pointer = static_cast<wroc_wayland_pointer*>(data);

    pointer->scroll({
        axis == WL_POINTER_AXIS_HORIZONTAL_SCROLL ? (value120 / 120.0) : 0.0,
        axis == WL_POINTER_AXIS_VERTICAL_SCROLL   ? (value120 / 120.0) : 0.0,
    });
}

static
void wroc_listen_wl_pointer_axis_relative_direction(void* data, wl_pointer*, u32 axis, u32 direction)
{
#if WROC_WAYLAND_BACKEND_NOISY_POINTER_AXIS
    log_debug("pointer_axis_relative_direction(axis = {}, direction = {})",
        core::to_string(wl_pointer_axis(axis)),
        core::to_string(wl_pointer_axis_relative_direction(direction)));
#endif
}

const wl_pointer_listener wroc_wl_pointer_listener {
    .enter                   = wroc_listen_wl_pointer_enter,
    .leave                   = wroc_listen_wl_pointer_leave,
    .motion                  = wroc_listen_wl_pointer_motion,
    .button                  = wroc_listen_wl_pointer_button,
    WROC_STUB_QUIET(axis),
    .frame                   = wroc_listen_wl_pointer_frame,
    WROC_STUB_QUIET(axis_source),
    WROC_STUB_QUIET(axis_stop),
    WROC_STUB_QUIET(axis_discrete),
    .axis_value120           = wroc_listen_wl_pointer_axis_value120,
    .axis_relative_direction = wroc_listen_wl_pointer_axis_relative_direction,
};

// -----------------------------------------------------------------------------

static
void relative_motion(void* data,
    zwp_relative_pointer_v1* zwp_relative_pointer_v1,
    u32 utime_hi,
    u32 utime_lo,
    wl_fixed_t dx,
    wl_fixed_t dy,
    wl_fixed_t dx_unaccel,
    wl_fixed_t dy_unaccel)
{
    auto* pointer = static_cast<wroc_wayland_pointer*>(data);
    if (!pointer->current_output->locked) return;
    auto delta = vec2f64(wl_fixed_to_double(dx_unaccel), wl_fixed_to_double(dy_unaccel));
    pointer->relative(delta);
}

const zwp_relative_pointer_v1_listener wroc_zwp_relative_pointer_v1_listener {
    .relative_motion = relative_motion,
};

wroc_wayland_pointer::~wroc_wayland_pointer()
{
    wl_pointer_release(wl_pointer);

    zwp_relative_pointer_v1_destroy(relative_pointer);
}

static
void wroc_pointer_set(wroc_wayland_backend* backend, struct wl_pointer* wl_pointer)
{
    if (!backend->pointer || backend->pointer->wl_pointer != wl_pointer) {
        log_debug("pointer_set({}, old = {})", (void*)wl_pointer, (void*)(backend->pointer ? backend->pointer->wl_pointer : nullptr));
    }

    if (backend->pointer && backend->pointer->wl_pointer == wl_pointer) {
        return;
    }

    auto* pointer = (backend->pointer = core::create<wroc_wayland_pointer>()).get();
    pointer->wl_pointer = wl_pointer;

    wl_pointer_add_listener(wl_pointer, &wroc_wl_pointer_listener, pointer);

    pointer->relative_pointer = zwp_relative_pointer_manager_v1_get_relative_pointer(backend->zwp_relative_pointer_manager_v1, wl_pointer);
    zwp_relative_pointer_v1_add_listener(pointer->relative_pointer, &wroc_zwp_relative_pointer_v1_listener, pointer);
    for (auto& output : backend->outputs) {
        wroc_wayland_backend_update_pointer_constraint(output.get());
    }

    server->seat->pointer->attach(pointer);
}

// -----------------------------------------------------------------------------

static
void wroc_listen_wl_keyboard_keymap(void* data, wl_keyboard*, u32 format, i32 fd, u32 size)
{
    // Upstream keymaps are ignored
    close(fd);
}

static
void wroc_listen_wl_keyboard_enter(void* data, wl_keyboard*, u32 serial, wl_surface*, wl_array* key_array)
{
    auto kb = static_cast<wroc_wayland_keyboard*>(data);

    kb->enter(wroc_to_span<u32>(key_array));
}

static
void wroc_listen_wl_keyboard_key(void* data, wl_keyboard*, u32 serial, u32 time, u32 keycode, u32 state)
{
    auto kb = static_cast<wroc_wayland_keyboard*>(data);

    switch (state) {
        break;case WL_KEYBOARD_KEY_STATE_PRESSED:  kb->press(keycode);
        break;case WL_KEYBOARD_KEY_STATE_RELEASED: kb->release(keycode);
    }
}

static
void wroc_listen_wl_keyboard_leave(void* data, wl_keyboard*, u32 serial, wl_surface*)
{
    auto kb = static_cast<wroc_wayland_keyboard*>(data);

    kb->leave();
}

const wl_keyboard_listener wroc_wl_keyboard_listener {
    .keymap      = wroc_listen_wl_keyboard_keymap,
    .enter       = wroc_listen_wl_keyboard_enter,
    .leave       = wroc_listen_wl_keyboard_leave,
    .key         = wroc_listen_wl_keyboard_key,
    WROC_STUB_QUIET(modifiers),
    WROC_STUB_QUIET(repeat_info),
};

wroc_wayland_keyboard::~wroc_wayland_keyboard()
{
    wl_keyboard_release(wl_keyboard);
}

static
void wroc_keyboard_set(wroc_wayland_backend* backend, struct wl_keyboard* wl_keyboard)
{
    if (!backend->keyboard || backend->keyboard->wl_keyboard != wl_keyboard) {
        log_debug("keyboard_set({}, old = {})", (void*)wl_keyboard, (void*)(backend->keyboard ? backend->keyboard->wl_keyboard : nullptr));
    }

    if (backend->keyboard && backend->keyboard->wl_keyboard == wl_keyboard) {
        return;
    }

    auto* keyboard = (backend->keyboard = core::create<wroc_wayland_keyboard>()).get();
    keyboard->wl_keyboard = wl_keyboard;

    wl_keyboard_add_listener(wl_keyboard, &wroc_wl_keyboard_listener, keyboard);

    server->seat->keyboard->attach(keyboard);
}

// -----------------------------------------------------------------------------

static
void wroc_listen_wl_seat_capabilities(void* data, wl_seat* seat, u32 capabilities)
{
    auto* backend = static_cast<wroc_wayland_backend*>(data);
    log_debug("wl_seat::capabilities");

    if (capabilities & WL_SEAT_CAPABILITY_KEYBOARD) {
        wroc_keyboard_set(backend, wl_seat_get_keyboard(seat));
    } else if (backend->keyboard) {
        backend->keyboard = nullptr;
    }

    if (capabilities & WL_SEAT_CAPABILITY_POINTER) {
        wroc_pointer_set(backend, wl_seat_get_pointer(seat));
    } else if (backend->pointer) {
        backend->pointer = nullptr;
    }
}

static
void wroc_listen_wl_seat_name(void* data, struct wl_seat*, const char* name)
{
    log_debug("wl_seat::name({})", name);
}

const wl_seat_listener wroc_wl_seat_listener {
    .capabilities = wroc_listen_wl_seat_capabilities,
    .name         = wroc_listen_wl_seat_name,
};
