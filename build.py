#!/bin/python

import os
import sys
import shutil
from pathlib import Path
import subprocess
import argparse
import filecmp

parser = argparse.ArgumentParser()
parser.add_argument("-U", "--update", action="store_true", help="Update")
parser.add_argument("-C", "--configure", action="store_true", help="Force configure")
parser.add_argument("-B", "--build", action="store_true", help="Build")
parser.add_argument("-S", "--shader", action="store_true", help="Build Shaders")
parser.add_argument("-R", "--release", action="store_true", help="Release")
parser.add_argument("-I", "--install", action="store_true", help="Install")
parser.add_argument("--asan", action="store_true", help="Enable Address Sanitizer")
parser.add_argument("--system-slangc", action="store_true", help="Use system slangc")
parser.add_argument("--system-linker", action="store_true", help="Use system linker (even if mold is available)")
parser.add_argument("--use-gcc", action="store_true", help="Use GCC instead of Clang")
args = parser.parse_args()

# -----------------------------------------------------------------------------

def ensure_dir(path: Path | str) -> Path:
    os.makedirs(path, exist_ok=True)
    return Path(path)

cwd = Path(".").absolute()
build_dir  = ensure_dir(cwd / ".build")
vendor_dir = ensure_dir(build_dir / "3rdparty")

# -----------------------------------------------------------------------------

def write_file_lazy(path: Path, data: str | bytes):
    match data:
        case bytes(b):
            if not path.exists() or b != path.read_bytes():
                path.write_bytes(b)
        case str(t):
            if not path.exists() or t != path.read_text():
                path.write_text(t)

# -----------------------------------------------------------------------------

import scripts.deps as deps

dep_dirs = deps.fetch_deps(vendor_dir, cwd / "build.json", args.update)

def dep_dir(name: str):
    return dep_dirs[name]

# -----------------------------------------------------------------------------

def cmake_build(src_dir: Path, build_dir: Path, install_dir, opts: list[str]):
    if not build_dir.exists() or args.update:
        cmd  = ["cmake", "-B", build_dir.absolute(), "-G", "Ninja"]
        cmd += [f"-DCMAKE_INSTALL_PREFIX={install_dir.absolute()}"]
        cmd += [ "-DCMAKE_INSTALL_MESSAGE=LAZY"]
        cmd += opts
        subprocess.run(cmd, cwd=src_dir)

    if not install_dir.exists() or args.update:
        cmd = ["cmake", "--build", build_dir.absolute(), "--target", "install"]
        subprocess.run(cmd, cwd=src_dir)

# -----------------------------------------------------------------------------

def build_slang() -> Path:
    source_dir  = dep_dir("slang")
    build_dir   = vendor_dir / "slang-build"
    install_dir = vendor_dir / "slang-install"

    cmake_build(source_dir, build_dir, install_dir, [])
    return install_dir / "bin/slangc"

if args.system_slangc:
    slangc = None
else:
    slangc = build_slang()

# -----------------------------------------------------------------------------

xwayland_satellite_dir = vendor_dir / "xwayland-satellite"
xwayland_satellite_bin = xwayland_satellite_dir / "target/release/xwayland-satellite"

def build_xwayland_satellite():
    if not xwayland_satellite_bin.exists() or args.update:
        subprocess.run(["cargo", "build", "--release"], cwd=xwayland_satellite_dir)

build_xwayland_satellite()

# -----------------------------------------------------------------------------

def list_wayland_protocols():
    wayland_protocols = []
    def add(path, name=None):
        wayland_protocols.append((path, name or path.stem))

    system_protocol_dir = Path("/usr/share/wayland-protocols")

    add(system_protocol_dir / "stable/xdg-shell/xdg-shell.xml")
    add(system_protocol_dir / "stable/linux-dmabuf/linux-dmabuf-v1.xml")
    add(system_protocol_dir / "stable/viewporter/viewporter.xml")
    add(system_protocol_dir / "stable/presentation-time/presentation-time.xml")
    add(system_protocol_dir / "stable/tablet/tablet-v2.xml")

    add(system_protocol_dir / "staging/cursor-shape/cursor-shape-v1.xml")
    add(system_protocol_dir / "staging/linux-drm-syncobj/linux-drm-syncobj-v1.xml")

    add(system_protocol_dir / "unstable/xdg-decoration/xdg-decoration-unstable-v1.xml")
    add(system_protocol_dir / "unstable/pointer-gestures/pointer-gestures-unstable-v1.xml")
    add(system_protocol_dir / "unstable/relative-pointer/relative-pointer-unstable-v1.xml")
    add(system_protocol_dir / "unstable/pointer-constraints/pointer-constraints-unstable-v1.xml")

    add(dep_dir("kde-protocols") / "src/protocols/server-decoration.xml")

    return wayland_protocols

def generate_wayland_protocols():

    wayland_scanner = "wayland-scanner" # Wayland scanner executable
    wayland_dir     = vendor_dir / "wayland"
    wayland_src     = ensure_dir(wayland_dir / "src")
    wayland_include = ensure_dir(wayland_dir / "include/wayland")
    wayland_client_include = ensure_dir(wayland_include / "client")
    wayland_server_include = ensure_dir(wayland_include / "server")

    cmake_target_name = "wayland-header"
    cmake_file = wayland_dir / "CMakeLists.txt"

    cmake = f"add_library({cmake_target_name}\n"

    for xml_path, name in list_wayland_protocols():

        header_name = f"{name}.h"

        # Generate client header
        header_path = wayland_client_include / header_name
        if not header_path.exists():
            cmd = [wayland_scanner, "client-header", xml_path, header_name]
            print(f"Generating wayland client header: {header_name}")
            subprocess.run(cmd, cwd = header_path.parent)

        # Generate server header
        header_path = wayland_server_include / header_name
        if not header_path.exists():
            cmd = [wayland_scanner, "server-header", xml_path, header_name]
            print(f"Generating wayland server header: {header_name}")
            subprocess.run(cmd, cwd = header_path.parent)

        # Generate source
        source_name = f"{name}-protocol.c"
        source_path = wayland_src / source_name
        if not source_path.exists():
            cmd = [wayland_scanner, "private-code", xml_path, source_name]
            print(f"Generating wayland source: {source_name}")
            subprocess.run(cmd, cwd = wayland_src)

        # Add source to CMakeLists
        cmake += f"    \"src/{source_name}\"\n"

    cmake += "    )\n"
    cmake += f"target_include_directories({cmake_target_name} PUBLIC include)\n"

    write_file_lazy(cmake_file, cmake)

generate_wayland_protocols()

# -----------------------------------------------------------------------------

def build_shaders():
    shaders = [
        ("src/wroc/shaders/blit.slang",  "wroc_blit_shader"),
        ("src/wroc/shaders/imgui.slang", "wroc_imgui_shader"),
        ("src/scene/render.slang",        "scene_render_shader"),
    ]

    shader_include_dirs = [
        "src"
    ]

    shader_gen_dir = ensure_dir(build_dir / "shaders")
    shader_gen_spv_dir     = ensure_dir(shader_gen_dir / "spv")
    shader_gen_include_dir = ensure_dir(shader_gen_dir / "include")
    shader_gen_source_dir  = ensure_dir(shader_gen_dir / "src")

    target_name = "shaders"

    cmake_path = shader_gen_dir / "CMakeLists.txt"
    cmake_out  = f"add_library({target_name})\n"
    cmake_out += f"target_include_directories({target_name} PUBLIC include)\n"
    cmake_out += f"target_compile_options({target_name} PRIVATE -std=c++26 -Wno-c23-extensions)\n"
    cmake_out += f"target_sources({target_name} PRIVATE\n"

    any_skipped = False

    for shader_src, prefix in shaders:

        cmake_out += f"    src/{prefix}.cpp\n"

        header_path = shader_gen_include_dir / f"{prefix}.hpp"
        source_path = shader_gen_source_dir  / f"{prefix}.cpp"

        if source_path.exists() and header_path.exists() and not args.shader:
            any_skipped = True
            continue

        tmp_path    = shader_gen_spv_dir / f"{prefix}.spv.tmp"
        spv_path    = shader_gen_spv_dir / f"{prefix}.spv"

        # Compile shader

        cmd  = ["slangc"]
        cmd += ["-o", tmp_path]
        cmd += ["-target", "spirv"]
        cmd += ["-fvk-use-entrypoint-name"]
        cmd += ["-emit-spirv-directly"]
        cmd += ["-matrix-layout-column-major"]
        cmd += ["-force-glsl-scalar-layout"]
        for i in shader_include_dirs:
            cmd += [f"-I{i}"]

        cmd += [shader_src]

        subprocess.run(cmd, executable=slangc)

        # SPIR-V binary

        write_file_lazy(spv_path, tmp_path.read_bytes())
        tmp_path.unlink()

        # C++ source

        source_out  = f"#include \"{prefix}.hpp\"\n"
        source_out +=  "\n"
        source_out += f"alignas(uint32_t) static constexpr unsigned char {prefix}_data[] {{\n"
        source_out += f"#embed \"../spv/{prefix}.spv\"\n"
        source_out +=  "};\n"
        source_out += f"extern const std::span<const uint32_t> {prefix}(reinterpret_cast<const uint32_t*>({prefix}_data), sizeof({prefix}_data) / 4);\n"
        write_file_lazy(source_path, source_out)

        # C++ header

        header_out  =  "#include <span>\n"
        header_out +=  "#include <cstdint>\n"
        header_out +=  "\n"
        header_out += f"extern const std::span<const uint32_t> {prefix};\n"
        write_file_lazy(header_path, header_out)

    cmake_out += "    )\n"
    write_file_lazy(cmake_path, cmake_out)

    return not any_skipped

if not build_shaders():
    print("Shader recompilation skipped (pass -S to recompile shaders)")

# -----------------------------------------------------------------------------

local_bin_dir  = ensure_dir(os.path.expanduser("~/.local/bin"))
xdg_portal_dir = ensure_dir(os.path.expanduser("~/.config/xdg-desktop-portal"))

def install_file(file: Path, target: Path):
    if target.exists():
        if filecmp.cmp(file, target):
            return
        os.remove(target)
    print(f"Installing [{file}] to [{target}]")
    shutil.copy2(file, target)

# -----------------------------------------------------------------------------

cxx_compilers = {
    "clang": "clang++",
    "gcc":   "g++",
}

def build(build_type, compiler, linker_type, install = None):
    build_name = f"{build_type.lower()}-{compiler}-{linker_type.lower()}"
    if args.asan:
        build_name += "-asan"
    cmake_dir = build_dir / build_name

    configure_ok = True

    if ((args.build or args.install) and not cmake_dir.exists()) or args.configure:
        cmd  = ["cmake", "--fresh", "-B", cmake_dir, "-G", "Ninja", f"-DVENDOR_DIR={vendor_dir}", "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"]
        cmd += [f"-DCMAKE_C_COMPILER={compiler}", f"-DCMAKE_CXX_COMPILER={cxx_compilers[compiler]}", f"-DCMAKE_LINKER_TYPE={linker_type}"]
        cmd += [f"-DCMAKE_BUILD_TYPE={build_type}"]
        if args.asan:
            cmd += ["-DUSE_ASAN=1"]

        print(cmd)
        res = subprocess.run(cmd)
        if res.returncode != 0:
            os._exit(res.returncode)

    if (cmake_dir / "compile_commands.json").exists():
        shutil.copy2(cmake_dir / "compile_commands.json", build_dir / "compile_commands.json")

    if configure_ok and (args.build or args.install):
        res = subprocess.run(["cmake", "--build", cmake_dir])
        if res.returncode != 0:
            os._exit(res.returncode)

    if install:
        install_file(cmake_dir / install, local_bin_dir / install)
        install_file(cwd / "resources/portals.conf", xdg_portal_dir / f"{install}-portals.conf")

# -----------------------------------------------------------------------------

use_mold = not args.system_linker and shutil.which("mold")

build(build_type  = "Release" if args.release else "Debug",
      compiler    = "gcc"     if args.use_gcc else "clang",
      linker_type = "MOLD"    if use_mold     else "SYSTEM",
      install     = "wroc"    if args.install else None)

# -----------------------------------------------------------------------------

if args.install:
    install_file(xwayland_satellite_bin, local_bin_dir / "xwayland-satellite")
