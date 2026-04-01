#include "libinput.hpp"

// -----------------------------------------------------------------------------

IoLibinputDevice::~IoLibinputDevice()
{
    libinput_device_unref(handle);
}

// -----------------------------------------------------------------------------

static
void handle_keyboard_key(IoLibinputDevice* device, libinput_event_keyboard* event)
{
    auto keycode = libinput_event_keyboard_get_key(event);

    switch (libinput_event_keyboard_get_key_state(event)) {
        break;case LIBINPUT_KEY_STATE_PRESSED:
            if (keycode == KEY_PAUSE) {
                log_error("PAUSE HIT - EMERGENCY SHUTDOWN");
                debug_kill();
            }
            io_input_device_key_press(device, keycode);
        break;case LIBINPUT_KEY_STATE_RELEASED:
            io_input_device_key_release(device, keycode);
    }
}

void IoLibinputDevice::update_leds(Flags<libinput_led> leds)
{
    libinput_device_led_update(handle, leds.get());
}

// -----------------------------------------------------------------------------

static
void device_init_pointer(IoLibinputDevice* device)
{
    if (libinput_device_config_accel_is_available(device->handle)) {
        libinput_device_config_accel_set_profile(device->handle, LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT);
        libinput_device_config_accel_set_speed(  device->handle, 0);
    }
}

static
void handle_pointer_button(IoLibinputDevice* device, libinput_event_pointer* event)
{
    auto button = libinput_event_pointer_get_button(event);

    switch (libinput_event_pointer_get_button_state(event)) {
        break;case LIBINPUT_BUTTON_STATE_PRESSED:  io_input_device_key_press(device, button);
        break;case LIBINPUT_BUTTON_STATE_RELEASED: io_input_device_key_release(device, button);
    }
}

static
void handle_pointer_motion(IoLibinputDevice* device, libinput_event_pointer* event)
{
    vec2f64 delta = {libinput_event_pointer_get_dx(event), libinput_event_pointer_get_dy(event)};

    io_input_device_pointer_motion(device, delta);
}

static
void handle_pointer_scroll_wheel(IoLibinputDevice* device, libinput_event_pointer* event)
{
    auto get = [&](libinput_pointer_axis axis) {
        return libinput_event_pointer_has_axis(event, axis)
            ? libinput_event_pointer_get_scroll_value_v120(event, axis) / 120.0
            : 0.0;
    };

    f64 dx = get(LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL);
    f64 dy = get(LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL);

    io_input_device_pointer_scroll(device, {dx, dy});
}

// -----------------------------------------------------------------------------

static
void handle_device_added(IoContext* io, struct libinput_device* libinput_device)
{
    int vendor = libinput_device_get_id_vendor(libinput_device);
    int product = libinput_device_get_id_product(libinput_device);
    const char* name = libinput_device_get_name(libinput_device);

    log_debug("Adding {} [{}:{}]", name, vendor, product);

    auto device = ref_create<IoLibinputDevice>();
    device->io = io;
    device->handle = libinput_device_ref(libinput_device);
    libinput_device_set_user_data(libinput_device, device.get());

    bool wanted = false;

    if (libinput_device_has_capability(libinput_device, LIBINPUT_DEVICE_CAP_KEYBOARD)) {
        wanted = true;
    }

    if (libinput_device_has_capability(libinput_device, LIBINPUT_DEVICE_CAP_POINTER)) {
        device_init_pointer(device.get());
        wanted = true;
    }

    if (wanted) {
        io_input_device_add(device.get());
        io->libinput->input_devices.emplace_back(device.get());
    }
}

static
void handle_device_removed(IoLibinputDevice* device)
{
    log_debug("Device removed - {}", libinput_device_get_name(device->handle));

    io_input_device_remove(device);
    device->io->libinput->input_devices.erase(device);
}

// -----------------------------------------------------------------------------

void io_libinput_handle_event(IoContext* io, libinput_event* event)
{
    auto type = libinput_event_get_type(event);
    if (type == LIBINPUT_EVENT_NONE) return;

    auto libinput_dev = libinput_event_get_device(event);
    auto device = static_cast<IoLibinputDevice*>(libinput_device_get_user_data(libinput_dev));
    if (!device && type != LIBINPUT_EVENT_DEVICE_ADDED) {
        log_error("libinput event has no associated device");
        return;
    }

    switch (type) {
        break;case LIBINPUT_EVENT_DEVICE_ADDED:   handle_device_added(io, libinput_dev);
        break;case LIBINPUT_EVENT_DEVICE_REMOVED: handle_device_removed(device);
        break;case LIBINPUT_EVENT_KEYBOARD_KEY:   handle_keyboard_key(device, libinput_event_get_keyboard_event(event));
        break;case LIBINPUT_EVENT_POINTER_BUTTON: handle_pointer_button(device, libinput_event_get_pointer_event(event));
        break;case LIBINPUT_EVENT_POINTER_MOTION: handle_pointer_motion(device, libinput_event_get_pointer_event(event));
        break;case LIBINPUT_EVENT_POINTER_AXIS:   /* ignored */
        break;case LIBINPUT_EVENT_POINTER_SCROLL_WHEEL: handle_pointer_scroll_wheel(device, libinput_event_get_pointer_event(event));
        break;default:
            log_warn("unhandled libinput event: {} ({})", type, u32(type));
    }
}
