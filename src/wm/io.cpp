#include "internal.hpp"

#include <io/io.hpp>

#include <core/math.hpp>

// -----------------------------------------------------------------------------
//      Outputs
// -----------------------------------------------------------------------------

static
void reflow_outputs(WindowManager* wm, bool any_changed = false)
{
    enum class LayoutDir { LeftToRight, RightToLeft };
    static constexpr LayoutDir dir = LayoutDir::RightToLeft;

    f32 x = 0;
    bool first = true;
    for (auto* output : wm->io.outputs) {
        auto size = output->io->info().size;
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
            wm_post_output_event(wm, ptr_to(WmOutputEvent {
                .type = WmEventType::output_configured,
                .output = output,
            }));
        }
    }

    if (any_changed) {
        wm_post_output_event(wm, ptr_to(WmOutputEvent {
            .type = WmEventType::output_layout,
        }));
        for (auto* output : wm->io.outputs) {
            output->io->request_frame();
        }
    }
}

static
auto find_output_for_io(WindowManager* wm, IoOutput* io_output) -> WmOutput*
{
    for (auto* output : wm->io.outputs) {
        if (output->io == io_output) {
            return output;
        }
    }
    return nullptr;
}

void wm_add_output_listener(WindowManager* wm, WmOutputListener listener)
{
    wm->io.output_listeners.emplace_back(std::move(listener));
}

void wm_post_output_event(WindowManager* wm, WmOutputEvent* event)
{
    for (auto& listener : wm->io.output_listeners) {
        listener(event);
    }
}

void wm_request_frame(WindowManager* wm)
{
    for (auto* output : wm->io.outputs) {
        output->io->request_frame();
    }
}

auto wm_output_get_viewport(WmOutput* output) -> rect2f32
{
    return output->viewport;
}

auto wm_list_outputs(WindowManager* wm) -> std::span<WmOutput* const>
{
    return wm->io.outputs;
}

auto wm_find_output_at(WindowManager* wm, vec2f32 point) -> WmFindOutputResult
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

static
void update_leds(WindowManager* wm, SeatKeyboard* keyboard)
{
    if (wm->io.led_devices.empty()) return;

    auto leds = seat_keyboard_get_leds(keyboard);

    for (auto& device : wm->io.led_devices) {
        device->update_leds(leds);
    }
}

static
void handle_key(WindowManager* wm, Seat* seat, const IoInputEvent& event, const IoInputChannel& channel)
{
    switch (channel.code) {
        break;case BTN_MOUSE ... BTN_TASK:
            seat_pointer_button(seat_get_pointer(seat), channel.code, channel.value, event.quiet);
        break;case KEY_ESC        ... KEY_MICMUTE:
              case KEY_OK         ... KEY_LIGHTS_TOGGLE:
              case KEY_ALS_TOGGLE ... KEY_PERFORMANCE: {
            auto keyboard = seat_get_keyboard(seat);
            auto changed = seat_keyboard_key(keyboard, channel.code, channel.value, event.quiet);
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
void handle_motion(WindowManager* wm, SeatPointer* pointer, vec2f64 rel_unaccel)
{
    auto rel_accel = apply_accel(rel_unaccel);
    auto position = wm_find_output_at(wm, seat_pointer_get_position(pointer) + rel_accel).position;
    seat_pointer_move(pointer, position, rel_accel, rel_unaccel);
}

static
void handle_input(WindowManager* wm, Seat* seat, const IoInputEvent& event)
{
    vec2f32 motion = {};
    vec2f32 scroll = {};

    for (auto& channel : event.channels) {
        switch (channel.type) {
            break;case EV_KEY:
                handle_key(wm, seat, event, channel);
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
void handle_input_region_damage(WindowManager* wm)
{
    for (auto* seat : wm_get_seats(wm)) {
        auto pointer = seat_get_pointer(seat);
        seat_pointer_move(pointer, seat_pointer_get_position(pointer), {}, {});
    }
}


static
void handle_input_added(WindowManager* wm, IoInputDevice* device)
{
    if (device->info().capabilities.contains(IoInputDeviceCapability::libinput_led)) {
        wm->io.led_devices.emplace_back(device);
    }
}

static
void handle_input_removed(WindowManager* wm, IoInputDevice* device)
{
    std::erase(wm->io.led_devices, device);
}

// -----------------------------------------------------------------------------
//      I/O Events
// -----------------------------------------------------------------------------

static
void handle_io_event(WindowManager* wm, IoEvent* event)
{
    switch (event->type) {
        // shutdown
        break;case IoEventType::shutdown_requested:
            io_stop(wm->io.context);

        // input
        break;case IoEventType::input_added:
            handle_input_added(wm, event->input.device);
        break;case IoEventType::input_removed:
            handle_input_removed(wm, event->input.device);
        break;case IoEventType::input_event:
            handle_input(wm, wm_get_seat(wm), event->input);

        // output
        break;case IoEventType::output_added: {
            auto output = ref_create<WmOutput>(event->output.output);
            wm->io.outputs.emplace_back(output.get());
            wm_post_output_event(wm, ptr_to(WmOutputEvent {
                .type = WmEventType::output_added,
                .output = output.get(),
            }));
            reflow_outputs(wm, true);
        }
        break;case IoEventType::output_configure:
            reflow_outputs(wm);
        break;case IoEventType::output_removed: {
            Ref output = find_output_for_io(wm, event->output.output);
            wm->io.outputs.erase_if([&](auto* p) { return p->io == event->output.output; });
            wm_post_output_event(wm, ptr_to(WmOutputEvent {
                .type = WmEventType::output_removed,
                .output = output.get(),
            }));
            reflow_outputs(wm, true);
        }

        break;case IoEventType::output_frame: {
            auto output = find_output_for_io(wm, event->output.output);

            wm_post_output_event(wm, ptr_to(WmOutputEvent {
                .type = WmEventType::output_frame,
                .output = output,
            }));

            // TODO: Only redraw with damage

            auto format = gpu_format_from_drm(DRM_FORMAT_ABGR8888);
            auto usage = GpuImageUsage::render;

            auto target = wm->io.pool->acquire({
                .extent = output->io->info().size,
                .format = format,
                .usage = usage,
                .modifiers = ptr_to(gpu_intersect_format_modifiers({
                    &gpu_get_format_properties(wm->gpu, format, usage)->mods,
                    &output->io->info().formats->get(format),
                }))
            });

            scene_render(wm->scene.get(), target.get(), output->viewport);

            output->io->commit(target.get(), gpu_flush(wm->gpu), IoOutputCommitFlag::vsync);
        }
    }
}

void wm_init_io(WindowManager* wm)
{
    wm->io.pool = gpu_image_pool_create(wm->gpu);

    io_set_event_handler(wm->io.context, [wm](IoEvent* event) {
        handle_io_event(wm, event);
    });

    scene_add_damage_listener(wm->scene.get(), [wm](SceneNode* node) {
        for (auto* output : wm->io.outputs) {
            output->io->request_frame();
        }

        if (dynamic_cast<SeatInputRegion*>(node)) {
            exec_enqueue(wm->exec, [wm = Weak(wm)] {
                if (wm) handle_input_region_damage(wm.get());
            });
        }
    });
}
