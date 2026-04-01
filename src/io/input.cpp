#include "internal.hpp"

#include "core/stack.hpp"

void io_input_device_add(IoInputDeviceBase* device)
{
    debug_assert(!std::ranges::contains(device->io->input_devices, device));
    device->io->input_devices.emplace_back(device);
    io_post_event(device->io, ptr_to(IoEvent {
        .type = IoEventType::input_added,
        .input = IoInputEvent {
            .device = device,
        },
    }));
}

void io_input_device_remove(IoInputDeviceBase* device)
{
    if (std::erase(device->io->input_devices, device)) {
        io_post_event(device->io, ptr_to(IoEvent {
            .type = IoEventType::input_removed,
            .input = IoInputEvent {
                .device = device,
            },
        }));
    }
}

static
void post_input(IoInputDeviceBase* device, bool quiet, std::span<const IoInputChannel> channels)
{
    io_post_event(device->io, ptr_to(IoEvent {
        .type = IoEventType::input_event,
        .input = IoInputEvent {
            .device = device,
            .quiet = quiet,
            .channels = channels,
        },
    }));
}

void io_input_device_leave(IoInputDeviceBase* device)
{
    ThreadStack stack;
    auto* events = stack.allocate<IoInputChannel>(device->pressed.size());
    usz count = 0;
    for (auto key : device->pressed) {
        events[count++] = {EV_KEY, key, 0};
    }
    if (count) {
        post_input(device, true, {events, count});
    }
    device->pressed.clear();
}

void io_input_device_key_enter(IoInputDeviceBase* device, std::span<const u32> keys)
{
    ThreadStack stack;
    auto* events = stack.allocate<IoInputChannel>(keys.size());
    usz count = 0;
    for (auto key : keys) {
        if (device->pressed.insert(key).second) {
            events[count++] = {EV_KEY, key, 1};
        }
    }
    if (count) {
        post_input(device, true, {events, count});
    }
}

void io_input_device_key_press(IoInputDeviceBase* device, u32 key)
{
    if (device->pressed.insert(key).second) {
        post_input(device, false, {{EV_KEY, key, 1}});
    }
}

void io_input_device_key_release(IoInputDeviceBase* device, u32 key)
{
    if (device->pressed.erase(key)) {
        post_input(device, false, {{EV_KEY, key, 0}});
    }
}

static
void post_rel2(IoInputDeviceBase* device, vec2u32 code, vec2f32 delta)
{
    IoInputChannel events[2];
    u32 count = 0;
    if (delta.x) events[count++] = {EV_REL, code.x, delta.x};
    if (delta.y) events[count++] = {EV_REL, code.y, delta.y};
    post_input(device, false, std::span(events, count));
}

void io_input_device_pointer_motion(IoInputDeviceBase* device, vec2f32 delta)
{
    post_rel2(device, {REL_X, REL_Y}, delta);
}

void io_input_device_pointer_scroll(IoInputDeviceBase* device, vec2f32 delta)
{
    post_rel2(device, {REL_HWHEEL, REL_WHEEL}, delta);
}
