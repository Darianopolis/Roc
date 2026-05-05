
#include "internal.hpp"

#include <core/math.hpp>

// -----------------------------------------------------------------------------
//      Outputs
// -----------------------------------------------------------------------------

static
void reflow_outputs(WmServer* wm, bool any_changed = false)
{
    enum class LayoutDir { LeftToRight, RightToLeft };
    static constexpr LayoutDir dir = LayoutDir::RightToLeft;

    f32 x = 0;
    bool first = true;
    for (auto* output : wm->io.outputs) {
        auto size = output->pixel_size;
        if constexpr (dir == LayoutDir::RightToLeft) {
            if (!std::exchange(first, false)) {
                x -= f32(size.x);
            }
        }
        auto last = output->viewport;
        output->viewport = {{x, 0.f}, size, xywh};
        if constexpr (dir == LayoutDir::LeftToRight) {
            x += f32(size.x);
        }

        if (last != output->viewport) {
            any_changed = true;
            wm_broadcast_event(wm, ptr_to(WmEvent {
                .output = {
                    .type = WmEventType::output_configured,
                    .output = output,
                }
            }));
        }
    }

    if (any_changed) {
        wm_broadcast_event(wm, ptr_to(WmEvent {
            .output = {
                .type = WmEventType::output_layout,
            }
        }));
        for (auto* output : wm->io.outputs) {
            output->interface.request_frame(output->userdata);
        }
    }
}

auto wm_output_create(WmServer* wm, void* userdata, WmOutputInterface interface) -> Ref<WmOutput>
{
    auto output = ref_create<WmOutput>();
    output->server = wm;
    output->userdata = userdata;
    output->interface = interface;

    wm->io.outputs.emplace_back(output.get());
    wm_broadcast_event(wm, ptr_to(WmEvent {
        .output = {
            .type = WmEventType::output_added,
            .output = output.get(),
        }
    }));
    reflow_outputs(wm, true);

    return output;
}

WmOutput::~WmOutput()
{
    std::erase(server->io.outputs, this);
    wm_broadcast_event(server, ptr_to(WmEvent {
        .output = {
            .type = WmEventType::output_removed,
            .output = this,
        }
    }));
    reflow_outputs(server, true);
}

void wm_output_set_pixel_size(WmOutput* output, vec2i32 pixel_size)
{
    output->pixel_size = pixel_size;
    reflow_outputs(output->server);
}

void wm_output_frame(WmOutput* output)
{
    auto* wm = output->server;

    wm_broadcast_event(wm, ptr_to(WmEvent {
        .output = {
            .type = WmEventType::output_frame,
            .output = output,
        }
    }));
}

auto wm_output_get_viewport(WmOutput* output) -> rect2f32
{
    return output->viewport;
}

void wm_request_frame(WmServer* wm)
{
    for (auto* output : wm->io.outputs) {
        output->interface.request_frame(output->userdata);
    }
}

auto wm_list_outputs(WmServer* wm) -> std::span<WmOutput* const>
{
    return wm->io.outputs;
}

auto wm_find_output_at(WmServer* wm, vec2f32 point) -> WmFindOutputResult
{
    vec2f32   best_position = point;
    f32       best_distance = INFINITY;
    WmOutput* best_output   = nullptr;
    for (auto* output : wm->io.outputs) {
        auto clamped = rect_clamp_point(output->viewport, point);
        if (point == clamped) {
            best_position = point;
            best_output = output;
            break;
        } else if (f32 dist = glm::distance(clamped, point); dist < best_distance) {
            best_position = clamped;
            best_distance = dist;
            best_output = output;
        }
    }
    return { best_output, best_position };
}

// -----------------------------------------------------------------------------
//      Inputs
// -----------------------------------------------------------------------------

auto wm_input_device_create(WmServer* wm, void* userdata, WmInputDeviceInterface interface) -> Ref<WmInputDevice>
{
    auto input_device = ref_create<WmInputDevice>();
    input_device->server = wm;
    input_device->userdata = userdata;
    input_device->interface = interface;
    wm->io.input_devices.emplace_back(input_device.get());
    return input_device;
}

WmInputDevice::~WmInputDevice()
{
    std::erase(server->io.input_devices, this);
}

static
void update_leds(WmServer* wm, SeatKeyboard* keyboard)
{
    if (wm->io.input_devices.empty()) return;

    auto leds = seat_keyboard_get_leds(keyboard);

    for (auto& device : wm->io.input_devices) {
        device->interface.update_leds(device->userdata, leds);
    }
}

static
void handle_key(WmServer* wm, Seat* seat, bool quiet, WmInputDeviceChannel channel)
{
    // TODO: Hacky fix to remap mouse side button to super key
    if (channel.code == BTN_SIDE) {
        channel.code = KEY_LEFTMETA;
    }

    switch (channel.code) {
        break;case BTN_MOUSE ... BTN_TASK:
            seat_pointer_button(seat_get_pointer(seat), channel.code, channel.value, quiet);
        break;case KEY_ESC        ... KEY_MICMUTE:
              case KEY_OK         ... KEY_LIGHTS_TOGGLE:
              case KEY_ALS_TOGGLE ... KEY_PERFORMANCE: {
            auto keyboard = seat_get_keyboard(seat);
            auto changed = seat_keyboard_key(keyboard, channel.code, channel.value, quiet);
            if (changed.contains(XKB_STATE_LEDS)) {
                update_leds(wm, keyboard);
            }
        }
        break;default:
            ;
    }
}

static
auto apply_accel(vec2f32 delta) -> vec2f32
{
    static constexpr f32 offset     = 2.f;
    static constexpr f32 rate       = 0.05f;
    static constexpr f32 multiplier = 0.3;

    // Apply a linear mouse acceleration curve
    //
    // Offset     - speed before acceleration is applied.
    // Accel      - rate that sensitivity increases with motion.
    // Multiplier - total multplier for sensitivity.
    //
    //      /
    //     / <- Accel
    // ___/
    //  ^-- Offset

    f32 speed = glm::length(delta);
    vec2f32 sens = vec2f32(multiplier * (1 + (std::max(speed, offset) - offset) * rate));

    return delta * sens;
}

static
void handle_motion(WmServer* wm, SeatPointer* pointer, vec2f64 rel_unaccel)
{
    auto rel_accel = apply_accel(rel_unaccel);
    auto position = wm_pointer_constraint_apply(wm, seat_pointer_get_position(pointer), rel_accel);
    position = wm_find_output_at(wm, position).position;
    seat_pointer_move(pointer, position, rel_accel, rel_unaccel);
}

void wm_input_device_push_events(WmInputDevice* input_device, bool quiet, std::span<WmInputDeviceChannel const> events)
{
    auto* wm = input_device->server;
    auto* seat = wm_get_seat(wm);

    vec2f32 motion = {};
    vec2f32 scroll = {};

    for (auto& channel : events) {
        switch (channel.type) {
            break;case EV_KEY:
                handle_key(wm, seat, quiet, channel);
            break;case EV_REL:
                switch (channel.code) {
                    break;case REL_X: motion.x += channel.value;
                    break;case REL_Y: motion.y += channel.value;
                    break;case REL_HWHEEL: scroll.x += channel.value;
                    break;case REL_WHEEL:  scroll.y += channel.value;
                }
            break;case EV_ABS:
                log_warn("Unknown  {} = {}", libevdev_event_code_get_name(channel.type, channel.code), channel.value);
        }
    }

    if (motion.x || motion.y) handle_motion(wm,   seat_get_pointer(seat), motion);
    if (scroll.x || scroll.y) seat_pointer_scroll(seat_get_pointer(seat), scroll);
}

static
void handle_input_region_damage(WmServer* wm)
{
    for (auto* seat : wm_get_seats(wm)) {
        auto pointer = seat_get_pointer(seat);
        seat_pointer_move(pointer, seat_pointer_get_position(pointer), {}, {});
    }
}

// -----------------------------------------------------------------------------
//      Initialization
// -----------------------------------------------------------------------------

void wm_init_io(WmServer* wm)
{
    scene_add_damage_listener(wm->scene.get(), [wm](SceneNode* node) {
        for (auto* output : wm->io.outputs) {
            output->interface.request_frame(output->userdata);
        }

        if (dynamic_cast<SceneInputRegion*>(node)) {
            exec_enqueue(wm->exec, [wm = Weak(wm)] {
                if (wm) handle_input_region_damage(wm.get());
            });
        }
    });
}
