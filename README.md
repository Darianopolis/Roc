# Wayland Roc

An experiment in writing a simple opinionated independent Wayland compositor.

## Goals

The following are guiding principals and goals in the development of Roc. **Not** a list of currently supported features.

- Independence from Wayland frameworks / libraries
   - Only relies on the `libwayland` wire protocol implementation
- Unified Vulkan compositing
   - Consolidating on a single graphics API and modern kernel simplifies GPU allocation, buffer sharing and synchronization code paths
- Protocol-independent display server
   - Open to additional protocol support
   - Minimal Wayland code to map protocol objects on to internal display server objects
   - Internal clients to implement in-process shell components / behaviours
      - Removes overhead of cross-process buffer sharing and synchronization
- Live configuration through native code
   - Extensions can be written to allow any preferred form of configuration

## Non-Goals

 - Support a wide range of (especially older) hardware
 - Support every Wayland protocol
    - A lot of Wayland protocol functionality can be replaced with compositor plugins that provide more integration and simpler communication.

# Support

Roc is currently tested on the following:

- Arch Linux + AMD Mesa (RADV)

# Architecture

Roc is built around an internal protocol-independent "display server", with a separate "window manager" component providing high-level behaviours and styling.

The Wayland protocol is then implemented as a set of internal clients, alongside other GUI shell components.

> The project is currently in a state of rewriting the old monolothic compositor implementation into a more more modular layered approach.
>
> Points marked as **❗** have not been implemented in the rewrite yet.

### Core Utillities - `core`

A collection of reusable components and helpers used throughout the project.

- Common data structures, algorithms and helpers
- Event loop (timed dispatch, file events)
- File descriptor lifetime management
- Object registry (intrusive allocation reference counting)
- Logging (level-based filtering, with full stacktrace contexts)
- Linear algebra (vectors, aabbs, rects)

### GPU Control - `gpu`

Small layer wrapping GPU  allocation, synchronization, and command execution.

- Vulkan object wrappers
- Event based completion handlers
- DMABUF / DRM syncobj interop

### Session I/O - `io`

Manages I/O, including input devices and display outputs; implemented as a set of subsystems:

- `process`  (child process management) **❗**
- `udev`     (device discovery, hotplug events) **❗**
- `session`  (VT switching, restricted device access) **❗**
- `libinput` (raw keyboard / mouse input) **❗**
- `evdev`    (raw gamepad / joystick input) **❗**
- `drm`      (atomic KMS display control) **❗**
- `wayland`  (nested emulation of `libinput`, `drm`)

### Scene Compositor - `scene`

An internal protocol-independent "display server".

- Scene graph
- Viewports
   - Per-viewport damage events **❗**
- Internal display server API
   - Virtual clients
- Per-client input routing
   - Implicit pointer focus based on input region nodes
   - Explicit keyboard/pointer focus based on client grabs
- Data management
   - Selection buffers
   - Drag and drop **❗**

### Immediate Mode UI - `ui`

An internal layer for writing in-process GUI clients.

- ImGui multi-viewport backend
   - First-class window layout integration
- Double-pumped event based frames
- Lazy per-`ImViewport` redraw on `ImDrawData` change **❗**

### Wayland Protocols - `way`

A thin layer adapting relevant Wayland protocols to `scene`'s internal API.

- Isolates compositor logic from Wayland "protocol plumbing"
- Maps `wl_client` to `scene.client`
- Builds surface trees out of `scene.texture` and `scene.input_region`
- Toplevels supplemented with `scene.window` for layout behaviour

### Window Manager - `wm`

Builds on `scene` to implement opinionated window management, styling, and behaviours.

- Output layout
- Mouse driver
   - Acceleration
   - Constraints **❗**
- Window decorations **❗**
- High-level window layout policy
   - Drag move/size
   - Grid based window placement **❗**
- Native shell components (launcher, system tray, notifications, etc..) **❗**
- Common keybindings (media keys, application launch, etc..) **❗**

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
- `--system-slangc` : Look for `slangc` on path

Build artifacts are placed into `.build/[debug|release](-asan)` depending on build parameters.
