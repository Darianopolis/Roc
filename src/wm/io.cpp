
#include "internal.hpp"

#include <core/math.hpp>
#include <core/log.hpp>

// -----------------------------------------------------------------------------
//      Outputs
// -----------------------------------------------------------------------------

static
void reflow_outputs(WmServer* server, bool any_changed = false)
{
    enum class LayoutDir { LeftToRight, RightToLeft };
    static constexpr LayoutDir dir = LayoutDir::RightToLeft;

    f32 x = 0;
    bool first = true;
    for (auto* output : server->io.outputs) {
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
            wm_broadcast_event(server, ptr_to(WmEvent {
                .output = {
                    .type = WmEventType::output_configured,
                    .output = output,
                }
            }));
            for (auto* window : server->windows) {
                if (wm_window_get_fullscreen(window) == output) {
                    wm_window_request_reposition(window, wm_output_get_viewport(output), vec2f32{1, 1});
                }
            }
        }
    }

    if (any_changed) {
        wm_broadcast_event(server, ptr_to(WmEvent {
            .output = {
                .type = WmEventType::output_layout,
            }
        }));
        for (auto* output : server->io.outputs) {
            output->interface.request_frame(output->userdata);
        }
    }
}

auto wm_output_create(WmServer* server, void* userdata, WmOutputInterface interface) -> Ref<WmOutput>
{
    auto output = ref_create<WmOutput>();
    output->server = server;
    output->userdata = userdata;
    output->interface = interface;

    server->io.outputs.emplace_back(output.get());
    wm_broadcast_event(server, ptr_to(WmEvent {
        .output = {
            .type = WmEventType::output_added,
            .output = output.get(),
        }
    }));
    reflow_outputs(server, true);

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

auto wm_output_get_viewport(WmOutput* output) -> rect2f32
{
    return output->viewport;
}

auto wm_output_get_workarea(WmOutput* output) -> rect2f32
{
    auto* server = output->server;

    auto workarea = output->viewport;
    workarea.origin += vec_cast<f32>(server->config.workarea.padding.tl);
    workarea.extent -= vec_cast<f32>(server->config.workarea.padding.tl + server->config.workarea.padding.br);
    return workarea;
}

void wm_request_frame(WmServer* server)
{
    for (auto* output : server->io.outputs) {
        output->interface.request_frame(output->userdata);
    }
}

auto wm_list_outputs(WmServer* server) -> std::span<WmOutput* const>
{
    return server->io.outputs;
}

auto wm_find_output_at(WmServer* server, vec2f32 point) -> WmFindOutputResult
{
    vec2f32   best_position = point;
    f32       best_distance = INFINITY;
    WmOutput* best_output   = nullptr;
    for (auto* output : server->io.outputs) {
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

auto wm_find_output_for(WmServer* server, rect2f32 rect) -> WmOutput*
{
    auto point = rect.origin + rect.extent / 2.f;
    return wm_find_output_at(server, point).output;
}

auto wm_find_output_for(WmServer* server, WmWindow* window) -> WmOutput*
{
    return wm_find_output_for(server, wm_window_get_frame(window));
}

// -----------------------------------------------------------------------------
//      Inputs
// -----------------------------------------------------------------------------

auto wm_input_device_create(WmServer* server, void* userdata, WmInputDeviceInterface interface) -> Ref<WmInputDevice>
{
    auto input_device = ref_create<WmInputDevice>();
    input_device->server = server;
    input_device->userdata = userdata;
    input_device->interface = interface;
    server->io.input_devices.emplace_back(input_device.get());
    return input_device;
}

WmInputDevice::~WmInputDevice()
{
    std::erase(server->io.input_devices, this);
}

static
void update_leds(WmServer* server, SeatKeyboard* keyboard)
{
    if (server->io.input_devices.empty()) return;

    auto leds = seat_keyboard_get_leds(keyboard);

    for (auto& device : server->io.input_devices) {
        device->interface.update_leds(device->userdata, leds);
    }
}

static
void handle_key(WmServer* server, Seat* seat, bool quiet, WmInputDeviceEvent channel)
{
    auto rebind = server->config.rebinds.find(channel.code);
    if (rebind != server->config.rebinds.end()) {
        channel.code = rebind->second;
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
                update_leds(server, keyboard);
            }
        }
        break;default:
            ; // TODO
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
void handle_motion(WmServer* server, SeatPointer* pointer, vec2f32 rel_unaccel)
{
    auto rel_accel = apply_accel(rel_unaccel);
    auto position = wm_pointer_constraint_apply(server, seat_pointer_get_position(pointer), rel_accel);
    position = wm_find_output_at(server, position).position;
    seat_pointer_move(pointer, position, rel_accel, rel_unaccel);
}

void wm_input_device_push_events(WmInputDevice* input_device, bool quiet, std::span<WmInputDeviceEvent const> events)
{
    auto* server = input_device->server;
    auto* seat = wm_get_seat(server);

    vec2f32 motion = {};
    vec2f32 scroll = {};

    for (auto& channel : events) {
        switch (channel.type) {
            break;case EV_KEY:
                handle_key(server, seat, quiet, channel);
            break;case EV_REL:
                switch (channel.code) {
                    break;case REL_X: motion.x += channel.value;
                    break;case REL_Y: motion.y += channel.value;
                    break;case REL_HWHEEL: scroll.x += channel.value;
                    break;case REL_WHEEL:  scroll.y += channel.value;
                }
            break;case EV_ABS:
                ; // TODO
        }
    }

    if (motion.x || motion.y) handle_motion(server,   seat_get_pointer(seat), motion);
    if (scroll.x || scroll.y) seat_pointer_scroll(seat_get_pointer(seat), scroll);
}

static
void handle_input_region_damage(WmServer* server)
{
    for (auto* seat : wm_get_seats(server)) {
        auto pointer = seat_get_pointer(seat);
        seat_pointer_move(pointer, seat_pointer_get_position(pointer), {}, {});
    }
}

// -----------------------------------------------------------------------------
//      Initialization
// -----------------------------------------------------------------------------

static
void handle_damage(WmServer* server, vec2f32 offset, const SceneDamage& damage)
{
    if (damage.types.contains(SceneDamageType::input)) {
        exec_enqueue(server->exec, [server = Weak(server)] {
            if (!server) return;
            handle_input_region_damage(server.get());
        });
    }

    if (damage.types.contains(SceneDamageType::visual)) {
        rect2f32 region = damage.region.bounds();
        region.origin += offset;

        for (auto* output : server->io.outputs) {
            if (rect_intersects(region, output->viewport)) {
                if (server->debug.show_damage) {
                    region2f32 damage_in = damage.region;
                    for (auto& band : damage_in.bands) {
                        band.min += offset.y;
                        band.max += offset.y;
                    }
                    for (auto& section : damage_in.sections) {
                        section.min += offset.x;
                        section.max += offset.x;
                    }

                    region2f32 damage_out;
                    region_op(damage_out, output->damage, damage_in, RegionOp::merge);
                    output->damage = std::move(damage_out);

                    // log_trace("damage sections {} bands {}", output->damage.sections.size(), output->damage.bands.size());
                }

                output->needs_redraw = true;
                output->interface.request_frame(output->userdata);
            }
        }
    }
}

void wm_init_io(WmServer* server)
{
    server->scene_damage_listener = wm_get_scene(server)->signals.damage.listen([server](vec2f32 offset, const SceneDamage& damage) {
        handle_damage(server, offset, damage);
    });
}
