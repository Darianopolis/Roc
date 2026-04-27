# Wayland Roc

An experiment in writing a simple opinionated independent Wayland compositor, with a few simple principals and goals:

- Independence from Wayland frameworks / libraries
   - Only relies on the `libwayland` wire protocol implementation
- Modern Vulkan based compositing
   - Consolidating on a single graphics API and modern kernel simplifies GPU allocation, buffer sharing and synchronization code paths
- Protocol-independent window manager interface
   - Open to additional protocol support
   - Minimal Wayland code to map protocol objects on to internal ones
- Live configuration through native code

## Non-Goals

 - Support a wide range of (especially older) hardware
 - Support every Wayland protocol
    - A lot of Wayland protocol functionality can be replaced with compositor plugins that provide more integration and simpler communication.

# Architecture

Roc is organized roughly into layers:

 - Core application support ─ `core`, `gpu`
 - Visual and input state ─ `scene`, `io`, `seat`
 - Window Manager ─ `wm`
 - Clients ─ `ui`, `way`, `shell`

```
     ┌─────── seat ─┐      ┌ ui ─┐
core ┴ gpu ┬─ scene ┼─ wm ─┤     ├─ shell
           └─ io ───┘      └ way ┘
```

### `core` ─ Common Utilities

A collection of reusable components and helpers used throughout the project.

- Common data structures, algorithms and helpers
- Event loop (timed dispatch, file events)
- File descriptor lifetime management
- Object registry (intrusive allocation reference counting)
- Structured logging (semantic + timestamp + stacktrace)
- Linear algebra (vectors, aabbs, rects)
- Threaded "execution contexts" with task enqueue and file listening

### `gpu` ─ GPU

Small layer wrapping GPU allocation, synchronization, and command execution.

- Event based completion handlers
- DMABUF / DRM syncobj interop
- Buffered command submission

### `io` ─ Session I/O

Manages I/O, including input devices and display outputs; implemented as a set of subsystems:

- `process`  (child process management)
- `udev`     (device discovery, hotplug events)
- `session`  (VT switching, restricted device access)
- `libinput` (raw keyboard / mouse input)
- `evdev`    (raw gamepad / joystick input)
- `drm`      (atomic KMS display control)
- `wayland`  (nested emulation of `libinput`, `drm`)

### `scene` ─ Scene Graph

A spatial hierarchy providing hit-testing, damage tracking, and rendering logic.

- Scene graph
- Damage listeners
- Core node types (tree, texture, mesh)

### `seat` ─ Seat

Input and data routing logic.

- Focus based input routing
- Data management
   - Selection buffers
   - Drag and drop

### `wm` ─ Window Manager

Internal client/server protocol for window management.

- Output layout
- Mouse
   - Acceleration
   - Constraints
- Window decorations
   - CSD clipping
   - Focus borders
- Interactions
   - Drag move/size
   - Drag grid window placement
   - Focus cycling

### `ui` ─ UI

An internal layer for writing in-process GUI clients.

- ImGui multi-viewport backend
   - First-class window layout integration
- Double-pumped event based frames
- Lazy scene mesh damage on `ImDrawData` change

### `way` ─ Wayland Server

A thin layer adapting relevant Wayland protocols to `wm`'s internal API.

- Isolates compositor and window management logic from Wayland "protocol plumbing"

### `shell` ─ Shell

Shell components and compositor entry point

- Ties together and provides configuration for all previous layers
- Native shell components (launcher, system tray, notifications, etc..)
- Common keybindings (media keys, application launch, etc..)

# Building

### System dependencies (build-time)

- python 3
- cmake
- ninja
- wayland-protocols
- xkbcommon
- libgio2
- mold (optional)
- clang/gcc (C++26 capable version at minimum)
- glslang

### Quickstart

Build in release mode and install to `.local/bin/roc`

```
$ python build.py -BIR
```

### Global Queue Priority

Roc can take advantage of higher queue scheduling priority when given the NICE system capability.

```
# setcap cap_sys_nice+ep ~/.local/bin/roc
```

### Build Options

- `-B` : Build project
- `-I` : Install project
- `-R` : Build in release mode
- `-C` : Force reconfigure and clean build
- `-U` : Check and update dependencies (updates `build.json`)
- `--asan` : Enable address sanitizer

Build artifacts are placed into `.build/[build-type]-[compiler]-[linker](-asan)`.
