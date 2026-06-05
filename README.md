# Roc

An experiment in writing a simple, opinionated, and independent desktop environment for educational and personal use.

- Protocol-independent shell and window manager
- Thin Wayland client adapter built on `libwayland`
- Standard I/O as a first class client protocol

## Non-Goals

- Support a wide range of (especially older) hardware
- Support generic Wayland configuration/shell protocols

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

Build in release mode and install to `~/.local/bin/roc`

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

Build artifacts are placed into `.build` (E.g. `.build/roc`)
