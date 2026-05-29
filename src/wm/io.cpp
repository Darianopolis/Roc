
#include "internal.hpp"

#include <core/math.hpp>
#include <core/log.hpp>

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
        output->viewport = {{x, 0.f}, vec_cast<f32>(size), xywh};
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
            for (auto* window : wm->windows) {
                if (wm_window_get_fullscreen(window) == output) {
                    wm_window_request_reposition(window, wm_output_get_viewport(output), vec2f32{1, 1});
                }
            }
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
    for (auto* window : server->windows) {
        if (wm_window_get_fullscreen(window) == this) {
            wm_window_set_fullscreen(window, nullptr);
        }
    }

    std::erase(server->io.outputs, this);
    wm_broadcast_event(server, ptr_to(WmEvent {
        .output = {
            .type = WmEventType::output_removed,
            .output = this,
        }
    }));
    reflow_outputs(server, true);
}

void wm_output_set_pixel_size(WmOutput* output, vec2u32 pixel_size)
{
    output->pixel_size = pixel_size;
    reflow_outputs(output->server);
}

auto wm_output_frame(WmOutput* output) -> bool
{
    auto* wm = output->server;

    if (output->bump_frame_id) {
        output->frame_id = ++wm->io.prev_frame_id;
        output->bump_frame_id = false;
    }

    wm_broadcast_event(wm, ptr_to(WmEvent {
        .output = {
            .type = WmEventType::output_frame,
            .output = output,
            .frame_id = output->frame_id,
        }
    }));

    if (output->needs_redraw) {
        output->needs_redraw = false;
        output->bump_frame_id = true;
        return true;
    }

    return false;
}

auto wm_output_get_viewport(WmOutput* output) -> rect2f32
{
    return output->viewport;
}

auto wm_output_get_workarea(WmOutput* output) -> rect2f32
{
    auto workarea = output->viewport;
    workarea.origin += vec_cast<f32>(wm_config.workarea.padding.tl);
    workarea.extent -= vec_cast<f32>(wm_config.workarea.padding.tl + wm_config.workarea.padding.br);
    return workarea;
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
        } else if (f32 dist = vec_distance(clamped, point); dist < best_distance) {
            best_position = clamped;
            best_distance = dist;
            best_output = output;
        }
    }
    return { best_output, best_position };
}

auto wm_find_output_for(WmServer* wm, WmWindow* window) -> WmOutput*
{
    auto point = scene_tree_get_position(window->root_tree.get()) + window->extent / 2.f;
    return wm_find_output_at(wm, point).output;
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

    f32 speed = vec_distance(delta, {});
    f32 sens = multiplier * (1 + (std::max(speed, offset) - offset) * rate);

    return delta * sens;
}

static
void handle_motion(WmServer* wm, SeatPointer* pointer, vec2f32 rel_unaccel)
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

static
void handle_damage(WmServer* wm, const SceneDamage& damage)
{
    if (damage.types.contains(SceneDamageType::input)) {
        exec_enqueue(wm->exec, [wm = Weak(wm)] {
            if (!wm) return;
            handle_input_region_damage(wm.get());
        });
    }

    if (damage.types.contains(SceneDamageType::visual)) {
        rect2f32 region = damage.region;
        for (auto* output : wm->io.outputs) {
            if (rect_intersects(region, output->viewport)) {
                output->needs_redraw = true;
                output->interface.request_frame(output->userdata);
            }
        }
    }
}

void wm_init_io(WmServer* wm)
{
    wm->scene_damage_listener = wm_get_scene(wm)->signals.damage.listen([wm](SceneDamage damage) {
        handle_damage(wm, damage);
    });
}
