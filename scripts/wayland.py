from .utils import *

def list_wayland_protocols(deps):
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

    add(deps["kde-protocols"] / "src/protocols/server-decoration.xml")

    return wayland_protocols

def generate_wayland_protocols(wayland_dir, deps):

    wayland_scanner = "wayland-scanner" # Wayland scanner executable
    wayland_src     = ensure_dir(wayland_dir / "src")
    wayland_include = ensure_dir(wayland_dir / "include/wayland")
    wayland_client_include = ensure_dir(wayland_include / "client")
    wayland_server_include = ensure_dir(wayland_include / "server")

    cmake_target_name = "wayland-header"
    cmake_file = wayland_dir / "CMakeLists.txt"

    cmake = f"add_library({cmake_target_name}\n"

    for xml_path, name in list_wayland_protocols(deps):

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
