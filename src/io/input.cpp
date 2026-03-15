#include "internal.hpp"

#include "core/stack.hpp"

void io_input_device_add(io_input_device_base* device)
{
    core_assert(!std::ranges::contains(device->ctx->input_devices, device));
    device->ctx->input_devices.emplace_back(device);
    io_post_event(device->ctx, core::ptr_to(io_event {
        .type = io_event_type::input_added,
        .input = io_input_event {
            .device = device,
        },
    }));
}

void io_input_device_remove(io_input_device_base* device)
{
    if (std::erase(device->ctx->input_devices, device)) {
        io_post_event(device->ctx, core::ptr_to(io_event {
            .type = io_event_type::input_removed,
            .input = io_input_event {
                .device = device,
            },
        }));
    }
}

static
void post_input(io_input_device_base* device, bool quiet, std::span<const io_input_channel> channels)
{
    io_post_event(device->ctx, core::ptr_to(io_event {
        .type = io_event_type::input_event,
        .input = io_input_event {
            .device = device,
            .quiet = quiet,
            .channels = channels,
        },
    }));
}

void io_input_device_leave(io_input_device_base* device)
{
    core::ThreadStack stack;
    auto* events = stack.allocate<io_input_channel>(device->pressed.size());
    usz count = 0;
    for (auto key : device->pressed) {
        events[count++] = {EV_KEY, key, 0};
    }
    if (count) {
        post_input(device, true, {events, count});
    }
    device->pressed.clear();
}

void io_input_device_key_enter(io_input_device_base* device, std::span<const u32> keys)
{
    core::ThreadStack stack;
    auto* events = stack.allocate<io_input_channel>(keys.size());
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

void io_input_device_key_press(io_input_device_base* device, u32 key)
{
    if (device->pressed.insert(key).second) {
        post_input(device, false, {{EV_KEY, key, 1}});
    }
}

void io_input_device_key_release(io_input_device_base* device, u32 key)
{
    if (device->pressed.erase(key)) {
        post_input(device, false, {{EV_KEY, key, 0}});
    }
}

static
void post_rel2(io_input_device_base* device, vec2u32 code, vec2f32 delta)
{
    io_input_channel events[2];
    u32 count = 0;
    if (delta.x) events[count++] = {EV_REL, code.x, delta.x};
    if (delta.y) events[count++] = {EV_REL, code.y, delta.y};
    post_input(device, false, std::span(events, count));
}

void io_input_device_pointer_motion(io_input_device_base* device, vec2f32 delta)
{
    post_rel2(device, {REL_X, REL_Y}, delta);
}

void io_input_device_pointer_scroll(io_input_device_base* device, vec2f32 delta)
{
    post_rel2(device, {REL_HWHEEL, REL_WHEEL}, delta);
}
