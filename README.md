# Roc

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

# Building

### System dependencies (build-time)

- python 3
- cmake
- ninja
- wayland-protocols
- xkbcommon
- mold (optional)
- gcc/clang (C++26 reflection capable)
- glslang

### Quickstart

Build in release mode and install to `.local/bin/roc`

```
$ python build.py -BRI
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
