#pragma once

#include "io.hpp"

void io_udev_init(  IoContext*);
void io_udev_deinit(IoContext*);

struct IoSession;
void io_session_init(  IoContext*);
void io_session_deinit(IoContext*);

struct IoLibinput;
void io_libinput_init(  IoContext*);
void io_libinput_deinit(IoContext*);

struct IoEvdev;
void io_evdev_init(  IoContext*);
void io_evdev_deinit(IoContext*);

struct IoDrm;
void io_drm_init(  IoContext*);
void io_drm_deinit(IoContext*);
void io_drm_start( IoContext*);

struct IoWayland;
void io_wayland_init(  IoContext*);
void io_wayland_deinit(IoContext *);
void io_wayland_start( IoContext*);

struct IoInputDeviceBase;
struct IoOutputBase;

struct IoContext
{
    IoSignals signals;

    Fd signal_fd;

    bool stop_requested = false;

    ExecContext* exec;
    Gpu* gpu;

    std::vector<IoInputDeviceBase*> input_devices;
    std::vector<IoOutputBase*>      outputs;

    struct udev* udev;

    Ref<IoSession>  session;
    Ref<IoLibinput> libinput; // input_device
    Ref<IoEvdev>    evdev;    // input_device
    Ref<IoDrm>      drm;      // output
    Ref<IoWayland>  wayland;  // output | input_device

    ~IoContext();
};

void io_request_shutdown(IoContext* io, IoShutdownReason reason);

// -----------------------------------------------------------------------------

struct IoOutputBase : IoOutput
{
    IoContext* io;

    bool frame_requested;

    vec2u32 size;

    // True if commit will accept a new frame
    bool commit_available = true;

    virtual void request_frame() final override;

    virtual ~IoOutputBase();
};

void io_output_try_redraw(IoOutputBase*);
void io_output_try_redraw_later(IoOutputBase*);
void io_output_post_configure(IoOutputBase*);

void io_output_add(   IoOutputBase*);
void io_output_remove(IoOutputBase*);

// -----------------------------------------------------------------------------

/**
 * Base type for input devices. There are three types of input devices:
 * 1. libinput - Handles keyboard/pointer/tablet/gesture/switch devices with seat access.
 * 2. wayland  - Handles the above devices when running in a nested Wayland session.
 * 3. evdev    - Handles all remaining input devices (gamepad/joystick/etc...) that do not require privileged seat access.
 */
struct IoInputDeviceBase : IoInputDevice
{
    IoContext* io;

    std::flat_set<u32> pressed;

    virtual ~IoInputDeviceBase() = default;
};

void io_post_event(IoContext*, IoEvent*);

void io_input_device_add(           IoInputDeviceBase*);
void io_input_device_remove(        IoInputDeviceBase*);
void io_input_device_leave(         IoInputDeviceBase*);
void io_input_device_key_enter(     IoInputDeviceBase*, std::span<const u32> keys);
void io_input_device_key_press(     IoInputDeviceBase*, u32 key);
void io_input_device_key_release(   IoInputDeviceBase*, u32 key);
void io_input_device_pointer_motion(IoInputDeviceBase*, vec2f32 delta);
void io_input_device_pointer_scroll(IoInputDeviceBase*, vec2f32 delta);
