#pragma once

#include <core/object.hpp>
#include <core/exec.hpp>
#include <core/signal.hpp>

#include <gpu/gpu.hpp>

// -----------------------------------------------------------------------------

struct IoContext;

// -----------------------------------------------------------------------------

enum class IoInputDeviceCapability : u32
{
    libinput_led = 1 << 0,
};

struct IoInputDeviceInfo
{
    Flags<IoInputDeviceCapability> capabilities;
};

/**
 * Generic raw input device interface.
 *
 * These input devices are modelled based on libinput and evdev, with normalized channel values.
 */
struct IoInputDevice
{
    virtual auto info() -> IoInputDeviceInfo = 0;
    virtual void update_leds(Flags<libinput_led>) {}
};

// -----------------------------------------------------------------------------

enum class IoOutputCommitFlag : u32
{
    vsync = 1 << 0,
};

struct IoOutputInfo
{
    vec2u32 size;
    const GpuFormatSet* formats;
};

/**
 * Generic output device interface.
 *
 * TODO: Support multi-plane configuration query and present.
 *       This interface is mostly temporary - the acquire/present
 *       model won't work for direct scanout of client-owned images.
 *       We'll also likely want to share swapchain logic (from `io/output.cpp`)
 *       with other systems - most notably screen video capture.
 */
struct IoOutput
{
    virtual auto info() -> IoOutputInfo = 0;
    virtual void request_frame() = 0;
    virtual void commit(GpuImage*, GpuSyncpoint done, Flags<IoOutputCommitFlag>) = 0;
};

// -----------------------------------------------------------------------------

enum class IoEventType
{
    shutdown_requested,

    input_added,
    input_removed,
    input_event,

    output_added,       // Sent when an output is first detected
    output_removed,     // Sent before a output is removed from the output list
    output_configure,   // Sent when an output's configuration changes
    output_frame,       // Sent when an output can accept a new frame of content
};

// -----------------------------------------------------------------------------

enum class IoShutdownReason
{
    no_more_outputs,    // Sent when no more outputs will be opened by the backend
    terminate_received, // Sent when SIGTERM is received
    interrupt_received, // Sent when SIGINT  is received
};

struct IoShutdownEvent
{
    IoEventType type;
    IoShutdownReason reason;
};

// -----------------------------------------------------------------------------

struct IoInputChannel
{
    u32 type;   // evdev type
    u32 code;   // evdev code
    f32 value;  // normalized channel value
};

/**
 * A grouping of associated input channel events.
 * This event reuses evdev codes for simplicity, with some minor changes:
 * - Values are sent as normalized floating point quantities.
 * - `REL_(H)WHEEL` events are sent with fractional detent deltas (~15 degrees / detent), instead of `REL_(H)WHEEL_HI_RES`.
 * - No `SYN_*` events are sent, instead `IoInputEvent` contains *all* events since the last report, and
 *   io always re-synchronizes internally.
 *
 * The `quiet` flag denotes that actions should not be taken in response to this event. E.g. on input
 * enter/leave events.
 *
 * Event design for touch, tablet and gesture controls is still pending.
 */
struct IoInputEvent
{
    IoEventType type;
    IoInputDevice* device;
    bool quiet;
    std::span<const IoInputChannel> channels;
};

// -----------------------------------------------------------------------------

struct IoOutputEvent
{
    IoEventType type;
    IoOutput*   output;
};

// -----------------------------------------------------------------------------

union IoEvent
{
    IoEventType type;

    IoShutdownEvent shutdown;
    IoInputEvent    input;
    IoOutputEvent   output;
};

using IoEventHandler = void(IoEvent*);

// -----------------------------------------------------------------------------

auto io_create(ExecContext*, Gpu*) -> Ref<IoContext>;

struct IoSignals
{
    Signal<void(IoEvent*)> event;
    Signal<void()> shutdown;
};

auto io_get_signals(IoContext*) -> IoSignals&;
void io_start(IoContext*);
void io_stop(IoContext*);

void io_output_create(IoContext*);
void io_output_destroy(IoOutput*);
