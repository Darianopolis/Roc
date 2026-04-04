cmake_minimum_required(VERSION 4.0)

project(roc)

# ------------------------------------------------------------------------------
#       Helpers
# ------------------------------------------------------------------------------

function(target_generated_source target visibility generated_source_name)
    string(JOIN "\n" generated_content ${ARGN})
    file(GENERATE
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${generated_source_name}"
        CONTENT "${generated_content}")
    target_sources(${target} ${visibility} "${CMAKE_CURRENT_BINARY_DIR}/${generated_source_name}")
endfunction()

function(init_target target_name)
    target_compile_options(${target_name} PRIVATE
        -std=c++26

        -Wall
        -Wextra
        -Werror
        -Wimplicit-fallthrough

        -Wno-missing-field-initializers
        -Wno-unknown-pragmas
        -Wno-unused-parameter
        )
    if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        target_compile_options(${target_name} PRIVATE
            -Wshadow # (only enabled for clang, as gcc complains on shadowing in constructors)

            -Wno-missing-designated-field-initializers
            -Wno-sometimes-uninitialized
            -Wno-delete-non-abstract-non-virtual-dtor # doesn't allow for protected non-virtual destructors

            # GNU Extensions
            -Wpedantic
            -Wno-gnu-case-range
            -Wno-zero-length-array
            -Wno-gnu-conditional-omitted-operand
            -Wno-nested-anon-types
            )
    elseif (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(${target_name} PRIVATE
            -Wno-changes-meaning # we use `foo* foo` a lot in wayland protocol bindings
            -Wno-maybe-uninitialized
            )
    endif()
    if(USE_ASAN)
        target_compile_options(${target_name} PUBLIC -fsanitize=address)
        target_link_libraries( ${target_name} PUBLIC asan)
    endif()
endfunction()

function(init_library target_name)
    add_library(${target_name})
    init_target(${target_name})
endfunction()

function(init_executable target_name)
    add_executable(${target_name})
    init_target(${target_name})
endfunction()

# ------------------------------------------------------------------------------
#       System Dependencies
# ------------------------------------------------------------------------------

find_package(PkgConfig REQUIRED)
pkg_check_modules(libevdev REQUIRED IMPORTED_TARGET libevdev)
pkg_check_modules(libgio2  REQUIRED IMPORTED_TARGET gio-2.0 gio-unix-2.0 gobject-2.0 glib-2.0)
pkg_check_modules(libdrm   REQUIRED IMPORTED_TARGET libdrm)

# ------------------------------------------------------------------------------
#       Git Dependencies
# ------------------------------------------------------------------------------

add_subdirectory(${VENDOR_DIR}/wayland EXCLUDE_FROM_ALL)

# ------------------------------------------------------------------------------

add_subdirectory(${VENDOR_DIR}/magic-enum EXCLUDE_FROM_ALL)

# ------------------------------------------------------------------------------

add_library(stb-image)
target_include_directories(stb-image PUBLIC ${VENDOR_DIR}/stb)
target_generated_source(stb-image PRIVATE stb_image.c       "#define STB_IMAGE_IMPLEMENTATION 1"       "#include <stb_image.h>")
target_generated_source(stb-image PRIVATE stb_image_write.c "#define STB_IMAGE_WRITE_IMPLEMENTATION 1" "#include <stb_image_write.h>")

# ------------------------------------------------------------------------------

add_subdirectory(${VENDOR_DIR}/glm EXCLUDE_FROM_ALL)

# ------------------------------------------------------------------------------

add_library(imgui)
target_include_directories(imgui PUBLIC ${VENDOR_DIR}/imgui)
target_include_directories(imgui PRIVATE src)
target_compile_definitions(imgui PUBLIC IMGUI_USER_CONFIG="ui/imconfig.h")
target_sources(imgui PRIVATE
    ${VENDOR_DIR}/imgui/imgui.cpp
    ${VENDOR_DIR}/imgui/imgui_demo.cpp
    ${VENDOR_DIR}/imgui/imgui_draw.cpp
    ${VENDOR_DIR}/imgui/imgui_tables.cpp
    ${VENDOR_DIR}/imgui/imgui_widgets.cpp
    ${VENDOR_DIR}/imgui/misc/cpp/imgui_stdlib.cpp
    )

# ------------------------------------------------------------------------------

add_subdirectory(${VENDOR_DIR}/unordered-dense EXCLUDE_FROM_ALL)

# ------------------------------------------------------------------------------

add_subdirectory(${VENDOR_DIR}/concurrent-queue EXCLUDE_FROM_ALL)

# ------------------------------------------------------------------------------

add_subdirectory(${VENDOR_DIR}/vulkan-headers           EXCLUDE_FROM_ALL)
add_subdirectory(${VENDOR_DIR}/vulkan-utility-libraries EXCLUDE_FROM_ALL)

# ------------------------------------------------------------------------------

add_subdirectory(${VENDOR_DIR}/vulkan-memory-allocator  EXCLUDE_FROM_ALL)

add_library(vma-object)
target_generated_source(   vma-object PRIVATE vk_mem_alloc.cpp "#define VMA_IMPLEMENTATION" "#include <vk_mem_alloc.h>")
target_compile_options(    vma-object PRIVATE -w)
target_link_libraries(     vma-object PUBLIC GPUOpen::VulkanMemoryAllocator Vulkan::Headers)
target_compile_definitions(vma-object PUBLIC "VMA_STATIC_VULKAN_FUNCTIONS=0" "VMA_DYNAMIC_VULKAN_FUNCTIONS=1")

# ------------------------------------------------------------------------------

add_library(renderdoc-api INTERFACE)
target_include_directories(renderdoc-api INTERFACE ${VENDOR_DIR}/renderdoc/renderdoc/api/app)

# ------------------------------------------------------------------------------
#       Core
# ------------------------------------------------------------------------------

init_library(core)
target_precompile_headers(core PUBLIC src/core/pch.hpp)
target_compile_definitions(core PUBLIC PROGRAM_NAME="roc" PROJECT_NAME="Roc")
target_sources(core PRIVATE
    src/core/log.cpp
    src/core/object.cpp
    src/core/fd.cpp
    src/core/stacktrace.cpp
    src/core/process.cpp
    src/core/debug.cpp

    src/exec/exec.cpp
    )
target_include_directories(core PUBLIC src)
target_link_libraries(core PUBLIC
    wayland-server
    wayland-client
    wayland-header
    xkbcommon
    PkgConfig::libevdev
    input
    stdc++exp
    magic_enum::magic_enum
    Vulkan::Headers
    Vulkan::UtilityHeaders
    vma-object
    stb-image
    glm::glm
    unordered_dense::unordered_dense
    concurrentqueue
    Xcursor
    PkgConfig::libdrm
    udev
    seat
    imgui
    cap
    PkgConfig::libgio2
    renderdoc-api
    )

# ------------------------------------------------------------------------------
#       GPU
# ------------------------------------------------------------------------------

add_subdirectory(.build/shaders)

init_library(gpu)
target_sources(gpu PRIVATE
    src/gpu/gpu.cpp
    src/gpu/functions.cpp
    src/gpu/buffer.cpp
    src/gpu/image.cpp
    src/gpu/descriptors.cpp
    src/gpu/formats.cpp
    src/gpu/pipeline.cpp
    src/gpu/commands.cpp
    src/gpu/semaphore.cpp
    src/gpu/image-pool.cpp
    )
target_link_libraries(gpu PUBLIC core shaders)

# ------------------------------------------------------------------------------
#       Session IO
# ------------------------------------------------------------------------------

init_library(io)
target_sources(io PRIVATE
    src/io/io.cpp
    src/io/output.cpp
    src/io/input.cpp
    src/io/drm/drm.cpp
    src/io/drm/properties.cpp
    src/io/evdev/evdev.cpp
    src/io/libinput/libinput.cpp
    src/io/libinput/events.cpp
    src/io/session/session.cpp
    src/io/udev/udev.cpp
    src/io/wayland/wayland.cpp
    src/io/wayland/output.cpp
    src/io/wayland/seat.cpp
    )
target_link_libraries(io PUBLIC core gpu)

init_executable(io-test)
target_sources(io-test PRIVATE src/io/main.cpp)
target_link_libraries(io-test PUBLIC io)

# ------------------------------------------------------------------------------
#       Scene Compositor
# ------------------------------------------------------------------------------

init_library(scene)
target_sources(scene PRIVATE
    src/scene/scene.cpp
    src/scene/render.cpp
    src/scene/node.cpp
    src/scene/window.cpp
    src/scene/seat.cpp
    src/scene/input.cpp
    src/scene/cursor.cpp
    src/scene/client.cpp
    src/scene/output.cpp
    src/scene/data.cpp
    )
target_link_libraries(scene PUBLIC io)

# ------------------------------------------------------------------------------
#       Window Manager
# ------------------------------------------------------------------------------

init_library(wm)
target_sources(wm PRIVATE
    src/wm/wm.cpp
    src/wm/io.cpp
    src/wm/seat.cpp
    src/wm/movesize.cpp
    src/wm/hotkeys.cpp
    src/wm/zone.cpp
    )
target_link_libraries(wm PUBLIC scene)

# ------------------------------------------------------------------------------
#       UI
# ------------------------------------------------------------------------------

init_library(ui)
target_sources(ui PRIVATE
    src/ui/ui.cpp
    )
target_link_libraries(ui PUBLIC scene)

# ------------------------------------------------------------------------------
#       Wayland
# ------------------------------------------------------------------------------

init_library(way)
target_sources(way PRIVATE
    src/way/client.cpp
    src/way/server.cpp
    src/way/util.cpp

    src/way/buffer/buffer.cpp
    src/way/buffer/dmabuf.cpp
    src/way/buffer/shm.cpp

    src/way/data/data.cpp

    src/way/output/output.cpp

    src/way/seat/cursor-shape.cpp
    src/way/seat/gestures.cpp
    src/way/seat/seat.cpp

    src/way/shell/popup.cpp
    src/way/shell/shell.cpp

    src/way/surface/decoration.cpp
    src/way/surface/subsurface.cpp
    src/way/surface/surface.cpp
    src/way/surface/viewport.cpp
    )
target_link_libraries(way PUBLIC scene)

# ------------------------------------------------------------------------------
#       Roc
# ------------------------------------------------------------------------------

init_executable(roc)
target_sources(roc PRIVATE
    src/roc/main.cpp
    src/roc/app/log-viewer.cpp
    src/roc/app/launcher.cpp
    src/roc/app/background.cpp
    )
target_link_libraries(roc PUBLIC wm ui way)
