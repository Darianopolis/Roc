# Roc

An experiment in writing a simple, independent desktop compositor.

- Protocol-independent core
- Thin Wayland client adapter built on libwayland

## Dependencies

PkgConfig:

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

Environment PATH:

- python
- cmake
- wayland-scanner
- gcc/clang (C++26 reflection capable)
- glslang

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
