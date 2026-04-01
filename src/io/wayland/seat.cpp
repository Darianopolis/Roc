#include "wayland.hpp"

IoWaylandKeyboard::~IoWaylandKeyboard()
{
    wl_keyboard_destroy(wl_keyboard);
}

static
void keyboard_enter(void* udata, wl_keyboard*, u32 serial, wl_surface*, wl_array* keys)
{
    auto* io = static_cast<IoContext*>(udata);
    auto* kb = io->wayland->keyboard.get();
    io_input_device_key_enter(kb, io_to_span<u32>(keys));

    io->wayland->in_keyboard_enter = true;
    wl_display_roundtrip(io->wayland->wl_display);
    io->wayland->in_keyboard_enter = false;
}

static
void keyboard_leave(void* udata, wl_keyboard*, u32 serial, wl_surface*)
{
    auto* io = static_cast<IoContext*>(udata);
    auto* kb = io->wayland->keyboard.get();
    io_input_device_leave(kb);
}

static
void keyboard_key(void* udata, wl_keyboard*, u32 serial, u32 time, u32 keycode, u32 state)
{
    auto* io = static_cast<IoContext*>(udata);
    auto* kb = io->wayland->keyboard.get();
    switch (state) {
        break;case WL_KEYBOARD_KEY_STATE_PRESSED:  io_input_device_key_press(  kb, keycode);
        break;case WL_KEYBOARD_KEY_STATE_RELEASED: io_input_device_key_release(kb, keycode);
    }
}

IO_WL_LISTENER(wl_keyboard) = {
    .keymap = [](void*, wl_keyboard*, u32, int fd, u32 size) { close(fd); },
    .enter = keyboard_enter,
    .leave = keyboard_leave,
    .key   = keyboard_key,
    IO_WL_STUB_QUIET(modifiers),
    IO_WL_STUB_QUIET(repeat_info),
};

static
void set_keyboard(IoContext* io)
{
    auto* kb = (io->wayland->keyboard = ref_create<IoWaylandKeyboard>()).get();
    kb->wl_keyboard = wl_seat_get_keyboard(io->wayland->wl_seat);
    kb->io = io;
    wl_keyboard_add_listener(kb->wl_keyboard, &io_wl_keyboard_listener, io);
    io_input_device_add(kb);
}

// -----------------------------------------------------------------------------

IoWaylandPointer::~IoWaylandPointer()
{
    zwp_relative_pointer_v1_destroy(zwp_relative_pointer_v1);
    wl_pointer_destroy(wl_pointer);
}

static
IoWaylandOutput* find_output_for_surface(IoContext* io, wl_surface* surface)
{
    for (auto* output : io->wayland->outputs) {
        if (output->wl_surface == surface) {
            return output;
        }
    }

    return nullptr;
}

static
void pointer_enter(void* udata, wl_pointer*, u32 serial, wl_surface* surface, wl_fixed_t sx, wl_fixed_t sy)
{
    auto* io = static_cast<IoContext*>(udata);
    auto* ptr = io->wayland->pointer.get();

    ptr->last_serial = serial;
    ptr->current_output = find_output_for_surface(io, surface);

    // Always hide parent compositor cursor
    wl_pointer_set_cursor(ptr->wl_pointer, serial, nullptr, 0, 0);
}

static
void pointer_leave(void* udata, wl_pointer*, u32 serial, wl_surface*)
{
    auto* ptr = static_cast<IoContext*>(udata)->wayland->pointer.get();
    ptr->last_serial = serial;
    ptr->current_output = nullptr;
    io_input_device_leave(ptr);
}

static
void pointer_button(void* udata, wl_pointer*, u32 serial, u32 time, u32 button, u32 state)
{
    auto* io = static_cast<IoContext*>(udata);
    auto* ptr = io->wayland->pointer.get();

    switch (state) {
        break;case WL_POINTER_BUTTON_STATE_PRESSED:
            if (!io->wayland->in_keyboard_enter) {
                io_input_device_key_press(ptr, button);
            }
        break;case WL_POINTER_BUTTON_STATE_RELEASED:
            io_input_device_key_release(ptr, button);
    }
}

static
void pointer_axis_value120(void* udata, wl_pointer*, u32 axis, i32 value120)
{
    auto* ptr = static_cast<IoContext*>(udata)->wayland->pointer.get();
    f32 value = value120 / 120.f;
    switch (axis) {
        break;case WL_POINTER_AXIS_HORIZONTAL_SCROLL: io_input_device_pointer_scroll(ptr, {value, 0});
        break;case WL_POINTER_AXIS_VERTICAL_SCROLL:   io_input_device_pointer_scroll(ptr, {0, value});
    }
}

IO_WL_LISTENER(wl_pointer) = {
    .enter = pointer_enter,
    .leave = pointer_leave,
    IO_WL_STUB_QUIET(motion),
    .button = pointer_button,
    IO_WL_STUB_QUIET(axis),
    IO_WL_STUB_QUIET(frame),
    IO_WL_STUB_QUIET(axis_source),
    IO_WL_STUB_QUIET(axis_stop),
    IO_WL_STUB_QUIET(axis_discrete),
    .axis_value120 = pointer_axis_value120,
    IO_WL_STUB_QUIET(axis_relative_direction),
};

static
void pointer_relative(
    void* udata,
    zwp_relative_pointer_v1*,
    u32 utime_hi, u32 utime_lo,
    wl_fixed_t dx, wl_fixed_t dy,
    wl_fixed_t dx_unaccel, wl_fixed_t dy_unaccel)
{
    auto* ptr = static_cast<IoWaylandPointer*>(udata);
    auto* output = get_impl(ptr->current_output.get());
    if (!output || !output->pointer_locked) return;
    io_input_device_pointer_motion(ptr, {wl_fixed_to_double(dx_unaccel), wl_fixed_to_double(dy_unaccel)});
}

IO_WL_LISTENER(zwp_relative_pointer_v1) = {
    .relative_motion = pointer_relative,
};

static
void set_pointer(IoContext* io)
{
    auto* ptr = (io->wayland->pointer = ref_create<IoWaylandPointer>()).get();
    ptr->io = io;

    ptr->wl_pointer = wl_seat_get_pointer(io->wayland->wl_seat);
    wl_pointer_add_listener(ptr->wl_pointer, &io_wl_pointer_listener, io);

    ptr->zwp_relative_pointer_v1 = zwp_relative_pointer_manager_v1_get_relative_pointer(
        io->wayland->zwp_relative_pointer_manager_v1,
        ptr->wl_pointer);
    zwp_relative_pointer_v1_add_listener(ptr->zwp_relative_pointer_v1, &io_zwp_relative_pointer_v1_listener, ptr);

    io_input_device_add(ptr);
}

// -----------------------------------------------------------------------------

static
void seat_capabilities(void* udata, wl_seat*, u32 capabilities)
{
    auto* io = static_cast<IoContext*>(udata);

    if      ( (capabilities & WL_SEAT_CAPABILITY_KEYBOARD) && !io->wayland->keyboard) set_keyboard(io);
    else if (!(capabilities & WL_SEAT_CAPABILITY_KEYBOARD))    io->wayland->keyboard = nullptr;

    if      ( (capabilities & WL_SEAT_CAPABILITY_POINTER) && !io->wayland->pointer) set_pointer(io);
    else if (!(capabilities & WL_SEAT_CAPABILITY_POINTER))    io->wayland->pointer = nullptr;
}

IO_WL_LISTENER(wl_seat) = {
    .capabilities = seat_capabilities,
    IO_WL_STUB(wl_seat, name),
};
