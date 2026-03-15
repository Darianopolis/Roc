#include "backend.hpp"
#include <wroc/event.hpp>

// -----------------------------------------------------------------------------

wroc_input_device::~wroc_input_device()
{
    libinput_device_unref(handle);
}

// -----------------------------------------------------------------------------

static
bool device_init_keyboard(wroc_input_device* device)
{
    device->keyboard = core::create<wroc_libinput_keyboard>();
    device->keyboard->base = device;

    server->seat->keyboard->attach(device->keyboard.get());

    return true;
}

static
void handle_keyboard_key(wroc_input_device* device, libinput_event_keyboard* event)
{
    auto keycode = libinput_event_keyboard_get_key(event);

    switch (libinput_event_keyboard_get_key_state(event)) {
        break;case LIBINPUT_KEY_STATE_PRESSED:
            if (keycode == KEY_PAUSE) {
                log_error("PAUSE HIT - EMERGENCY SHUTDOWN");
                core::debugkill();
            }
            device->keyboard->press(keycode);
        break;case LIBINPUT_KEY_STATE_RELEASED:
            device->keyboard->release(keycode);
    }
}

void wroc_libinput_keyboard::update_leds(libinput_led leds)
{
    libinput_device_led_update(base->handle, leds);
}

// -----------------------------------------------------------------------------

static
bool device_init_pointer(wroc_input_device* device)
{
    device->pointer = core::create<wroc_libinput_pointer>();
    device->pointer->base = device;

    if (libinput_device_config_accel_is_available(device->handle)) {
        libinput_device_config_accel_set_profile(device->handle, LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT);
        libinput_device_config_accel_set_speed(  device->handle, 0);
    }

    auto* pointer = device->pointer.get();

    server->seat->pointer->attach(pointer);

    return true;
}

static
void handle_pointer_button(wroc_input_device* device, libinput_event_pointer* event)
{
    auto button = libinput_event_pointer_get_button(event);

    switch (libinput_event_pointer_get_button_state(event)) {
        break;case LIBINPUT_BUTTON_STATE_PRESSED:  device->pointer->press(button);
        break;case LIBINPUT_BUTTON_STATE_RELEASED: device->pointer->release(button);
    }
}

static
void handle_pointer_motion(wroc_input_device* device, libinput_event_pointer* event)
{
    auto* pointer = device->pointer.get();

    vec2f64 delta = {libinput_event_pointer_get_dx(event), libinput_event_pointer_get_dy(event)};

    pointer->relative(delta);
}

static
void handle_pointer_scroll_wheel(wroc_input_device* device, libinput_event_pointer* event)
{
    auto get = [&](libinput_pointer_axis axis) {
        return libinput_event_pointer_has_axis(event, axis)
            ? libinput_event_pointer_get_scroll_value_v120(event, axis) / 120.0
            : 0.0;
    };

    f64 dx = get(LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL);
    f64 dy = get(LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL);

    device->pointer->scroll({ dx, dy });
}

// -----------------------------------------------------------------------------

static
void handle_device_added(wroc_direct_backend* backend, struct libinput_device* libinput_device)
{
    int vendor = libinput_device_get_id_vendor(libinput_device);
    int product = libinput_device_get_id_product(libinput_device);
    const char* name = libinput_device_get_name(libinput_device);

    log_debug("Adding {} [{}:{}]", name, vendor, product);

    auto device = core::create<wroc_input_device>();
    device->backend = backend;
    device->handle = libinput_device_ref(libinput_device);
    libinput_device_set_user_data(libinput_device, device.get());

    bool want_device = false;
    if (libinput_device_has_capability(libinput_device, LIBINPUT_DEVICE_CAP_KEYBOARD)) {
        log_debug("  has keyboard capability");
        want_device = device_init_keyboard(device.get());
    }

    if (libinput_device_has_capability(libinput_device, LIBINPUT_DEVICE_CAP_POINTER)) {
        log_debug("  has pointer capability");
        want_device = device_init_pointer(device.get());
    }

    if (want_device) {
        backend->input_devices.emplace_back(device);
    }
}

static
void handle_device_removed(wroc_input_device* device)
{
    log_debug("Device removed - {}", libinput_device_get_name(device->handle));

    std::erase_if(device->backend->input_devices, [&](const auto& d) { return d.get() == device; });
}

// -----------------------------------------------------------------------------

void wroc_backend_handle_libinput_event(wroc_direct_backend* backend, libinput_event* event)
{
    // auto type = libinput_event_get_type(event);
    // log_debug("libinput event: {}", core::to_string(type));

    auto event_type = libinput_event_get_type(event);
    if (event_type == LIBINPUT_EVENT_NONE) return;

    auto libinput_dev = libinput_event_get_device(event);
    auto device = static_cast<wroc_input_device*>(libinput_device_get_user_data(libinput_dev));
    if (!device && event_type != LIBINPUT_EVENT_DEVICE_ADDED) {
        log_error("libinput event has no associated device");
        return;
    }

    switch (event_type) {
        break;case LIBINPUT_EVENT_DEVICE_ADDED:   handle_device_added(backend, libinput_dev);
        break;case LIBINPUT_EVENT_DEVICE_REMOVED: handle_device_removed(device);
        break;case LIBINPUT_EVENT_KEYBOARD_KEY:   handle_keyboard_key(device, libinput_event_get_keyboard_event(event));
        break;case LIBINPUT_EVENT_POINTER_BUTTON: handle_pointer_button(device, libinput_event_get_pointer_event(event));
        break;case LIBINPUT_EVENT_POINTER_MOTION: handle_pointer_motion(device, libinput_event_get_pointer_event(event));
        break;case LIBINPUT_EVENT_POINTER_AXIS:   /* ignored */
        break;case LIBINPUT_EVENT_POINTER_SCROLL_WHEEL: handle_pointer_scroll_wheel(device, libinput_event_get_pointer_event(event));
        break;default:
            log_warn("unhandled libinput event: {} ({})", core::to_string(event_type), u32(event_type));
    }
}
