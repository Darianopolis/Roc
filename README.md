# Roc

An experiment in writing a simple, independent desktop environment for personal and educational use.

- Protocol-independent core components
- Thin Wayland client adapter built on `libwayland`

## Dependencies

The following packages must be discoverable by PkgConfig:

- libevdev
- libdrm
- libudev
- libcap
- libseat
- libinput
- freetype2
- xkbcommon
- xcursor
- wayland-server
- wayland-client

The following executables must be available on the environment path:

- python
- cmake
- wayland-scanner
- gcc/clang (C++26 reflection capable)
- glslang

The following packages must also be discoverable by CMake:

- ninja
- mold (optional)

## Build

Build in release mode and install to `~/.local/bin/roc`

```
$ python build.py -BRI
```

### Global Queue Priority

Roc can take advantage of higher queue scheduling priority when given the NICE system capability.

```
# setcap cap_sys_nice+ep ~/.local/bin/roc
```

### Options

Pass `-h` or `--help` to see build options

Build artifacts are placed into `.build` (E.g. `.build/roc`)
