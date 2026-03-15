#pragma once

#include "io.hpp"

#define IO_BACKEND(Name) \
    struct Name; \
    void Name##_init(io_context*)

IO_BACKEND(io_udev);
IO_BACKEND(io_session);
IO_BACKEND(io_libinput);
IO_BACKEND(io_evdev);
IO_BACKEND(io_drm);
IO_BACKEND(io_wayland);

void io_wayland_start(io_context*);

struct io_input_device_base;
struct io_output_base;

struct io_context
{
    std::move_only_function<io_event_handler> event_handler;

    bool stop_requested = false;

    core::EventLoop* event_loop;
    gpu::Context*     gpu;

    std::vector<io_input_device_base*> input_devices;
    std::vector<io_output_base*>       outputs;

    core::Ref<io_udev>     udev;
    core::Ref<io_session>  session;
    core::Ref<io_libinput> libinput; // input_device
    core::Ref<io_evdev>    evdev;    // input_device
    core::Ref<io_drm>      drm;      // output
    core::Ref<io_wayland>  wayland;  // output | input_device

    ~io_context();
};

void io_request_shutdown(io_context* ctx, io_shutdown_reason reason);

// -----------------------------------------------------------------------------

struct io_output_base : io_output
{
    io_context* ctx;

    bool frame_requested;

    vec2u32 size;

    // True if commit will accept a new frame
    bool commit_available = true;

    virtual void request_frame() final override;

    virtual ~io_output_base();
};

void io_output_try_redraw(io_output_base*);
void io_output_try_redraw_later(io_output_base*);
void io_output_post_configure(io_output_base*);

void io_output_add(   io_output_base*);
void io_output_remove(io_output_base*);

// -----------------------------------------------------------------------------

/**
 * Base type for input devices. There are three types of input devices:
 * 1. libinput - Handles keyboard/pointer/tablet/gesture/switch devices with seat access.
 * 2. wayland  - Handles the above devices when running in a nested Wayland session.
 * 3. evdev    - Handles all remaining input devices (gamepad/joystick/etc...) that do not require privileged seat access.
 */
struct io_input_device_base : io_input_device
{
    io_context* ctx;

    core::Flags<io_input_device_capability> capabilities;

    core::FlatSet<u32> pressed;

    auto info() -> io_input_device_info
    {
        return { .capabilities = capabilities };
    }

    virtual ~io_input_device_base() = default;
};

void io_post_event(io_context*, io_event*);

void io_input_device_add(           io_input_device_base*);
void io_input_device_remove(        io_input_device_base*);
void io_input_device_leave(         io_input_device_base*);
void io_input_device_key_enter(     io_input_device_base*, std::span<const u32> keys);
void io_input_device_key_press(     io_input_device_base*, u32 key);
void io_input_device_key_release(   io_input_device_base*, u32 key);
void io_input_device_pointer_motion(io_input_device_base*, vec2f32 delta);
void io_input_device_pointer_scroll(io_input_device_base*, vec2f32 delta);
