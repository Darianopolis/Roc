from .utils import *

def build_shaders(cwd, build_dir):
    shaders = [
        ("src/scene/shader/render.frag.glsl", "scene_render_frag", "frag"),
        ("src/scene/shader/render.vert.glsl", "scene_render_vert", "vert"),
    ]

    shader_gen_dir         = ensure_dir(build_dir / "shaders")
    shader_gen_spv_dir     = ensure_dir(shader_gen_dir / "spv")
    shader_gen_include_dir = ensure_dir(shader_gen_dir / "include")
    shader_gen_source_dir  = ensure_dir(shader_gen_dir / "src")
    shader_gen_stamp_dir   = ensure_dir(shader_gen_dir / "stamp")

    target_name = "shaders"

    cmake_path = shader_gen_dir / "CMakeLists.txt"
    cmake_out  = f"add_library({target_name})\n"
    cmake_out += f"target_include_directories({target_name} PUBLIC include)\n"
    cmake_out += f"target_compile_options({target_name} PRIVATE -std=c++26 -Wno-c23-extensions)\n"
    cmake_out += f"target_sources({target_name} PRIVATE\n"

    # Dependency tracking: glslang doesn't give us dependency info,
    # so we just check mtime against all .glsl/.h files.
    shader_source_files = list(cwd.glob("src/**/*.glsl")) + list(cwd.glob("src/**/*.h"))
    shader_dep_mtime = max((f.stat().st_mtime for f in shader_source_files if f.exists()), default=0.0)

    for src_file, prefix, stage_flag in shaders:
        cmake_out += f"    src/{prefix}.cpp\n"

        src_path    = cwd / src_file
        spv_path    = shader_gen_spv_dir     / f"{prefix}.spv"
        header_path = shader_gen_include_dir / f"{prefix}.hpp"
        source_path = shader_gen_source_dir  / f"{prefix}.cpp"
        stamp_path  = shader_gen_stamp_dir   / f"{prefix}.stamp"

        def needs_rebuild() -> bool:
            return (any(not f.exists() for f in [stamp_path, spv_path, header_path, source_path])
                    or shader_dep_mtime > stamp_path.stat().st_mtime)

        if not needs_rebuild():
            continue

        print(f"Compiling shader: {src_path} [{stage_flag}] as {prefix}")

        tmp_path = shader_gen_spv_dir / f"{prefix}.spv.tmp"

        cmd  = ["glslang"]
        cmd += ["-V"]
        cmd += ["-S", stage_flag]
        cmd += ["-Isrc"]
        cmd += ["--target-env", "vulkan1.4"]
        cmd += ["-o", tmp_path]
        cmd += [str(src_path)]

        res = subprocess.run(cmd)
        if res.returncode != 0 or not tmp_path.exists():
            print("Shader compilation failed")
            os._exit(1)

        # SPIR-V binary

        write_file_lazy(spv_path, tmp_path.read_bytes())
        tmp_path.unlink()
        stamp_path.touch()

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