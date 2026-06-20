#include "internal.hpp"

#include <core/stack.hpp>

void io_input_device_add(IoInputDeviceBase* device)
{
    debug_assert(!device->input_device);
    device->input_device = wm_input_device_create(device->io->wm, device, WmInputDeviceInterface {
        .update_leds = [](void* data, Flags<libinput_led> leds) {
            static_cast<IoInputDeviceBase*>(data)->update_leds(leds);
        }
    });
}

void io_input_device_post(IoInputDeviceBase* device, bool quiet, std::span<const WmInputDeviceEvent> channels)
{
    ThreadStack stack;
    auto out = stack.allocate<WmInputDeviceEvent>(channels.size());
    u32 count = 0;
    for (auto channel : channels) {
        switch (channel.type) {
            break;case EV_KEY:
                if (channel.value ? !device->pressed.insert(channel.code).second : !device->pressed.erase(channel.code)) {
                    goto skip;
                }
            break;case EV_REL:
                if (channel.value == 0) {
                    goto skip;
                }
        }

        out[count++] = channel;
    skip:
    }
    wm_input_device_push_events(device->input_device.get(), quiet, {out, count});
}
