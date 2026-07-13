cmake_minimum_required(VERSION 4.0)

project(Roc C CXX ASM)

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
    target_precompile_headers(${target_name} PRIVATE src/core/pch.hpp)
    target_compile_options(${target_name} PRIVATE
        -std=c++26

        -Wall
        -Wextra
        -Werror
        -Wimplicit-fallthrough

        -Wno-missing-field-initializers
        -Wno-unknown-pragmas
        -Wno-unused-parameter
        -Wno-invalid-offsetof
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
            -freflection

            -Wno-changes-meaning # we use `foo* foo` a lot in wayland protocol bindings
            -Wno-maybe-uninitialized
            )
    endif()
    if(USE_ASAN)
        target_compile_options(${target_name} PUBLIC -fsanitize=address)
        target_link_options(   ${target_name} PUBLIC -fsanitize=address)
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

pkg_check_modules(libevdev       REQUIRED IMPORTED_TARGET libevdev)
pkg_check_modules(libdrm         REQUIRED IMPORTED_TARGET libdrm)
pkg_check_modules(libseat        REQUIRED IMPORTED_TARGET libseat)
pkg_check_modules(wayland-server REQUIRED IMPORTED_TARGET wayland-server)
pkg_check_modules(wayland-client REQUIRED IMPORTED_TARGET wayland-client)
pkg_check_modules(xkbcommon      REQUIRED IMPORTED_TARGET xkbcommon)
pkg_check_modules(libinput       REQUIRED IMPORTED_TARGET libinput)
pkg_check_modules(xcursor        REQUIRED IMPORTED_TARGET xcursor)
pkg_check_modules(libudev        REQUIRED IMPORTED_TARGET libudev)
pkg_check_modules(libcap         REQUIRED IMPORTED_TARGET libcap)
pkg_check_modules(freetype       REQUIRED IMPORTED_TARGET freetype2)

# ------------------------------------------------------------------------------
#       Git Dependencies
# ------------------------------------------------------------------------------

add_subdirectory(${BUILD_DIR}/wayland EXCLUDE_FROM_ALL)

# ------------------------------------------------------------------------------

add_library(stb-image)
target_include_directories(stb-image PUBLIC ${VENDOR_DIR}/stb)
target_generated_source(stb-image PRIVATE stb_image.c       "#define STB_IMAGE_IMPLEMENTATION 1"       "#include <stb_image.h>")
target_generated_source(stb-image PRIVATE stb_image_write.c "#define STB_IMAGE_WRITE_IMPLEMENTATION 1" "#include <stb_image_write.h>")
add_library(vendor::stb-image ALIAS stb-image)

# ------------------------------------------------------------------------------

add_subdirectory(${VENDOR_DIR}/unordered-dense EXCLUDE_FROM_ALL)

# ------------------------------------------------------------------------------

add_subdirectory(${VENDOR_DIR}/vulkan-headers EXCLUDE_FROM_ALL)

# ------------------------------------------------------------------------------

add_subdirectory(${VENDOR_DIR}/vulkan-memory-allocator EXCLUDE_FROM_ALL)

add_library(vma-object)
target_generated_source(vma-object PRIVATE vk_mem_alloc.cpp "#define VMA_IMPLEMENTATION" "#include <vk_mem_alloc.h>")
target_compile_options(vma-object PRIVATE -w)
target_link_libraries(vma-object PUBLIC GPUOpen::VulkanMemoryAllocator Vulkan::Headers)
target_compile_definitions(vma-object PUBLIC "VMA_STATIC_VULKAN_FUNCTIONS=0" "VMA_DYNAMIC_VULKAN_FUNCTIONS=1")
add_library(vendor::vma-object ALIAS vma-object)

# ------------------------------------------------------------------------------

add_library(renderdoc-api INTERFACE)
target_include_directories(renderdoc-api INTERFACE ${VENDOR_DIR}/renderdoc/renderdoc/api/app)
add_library(vendor::renderdoc-api ALIAS renderdoc-api)

# ------------------------------------------------------------------------------
#       Core
# ------------------------------------------------------------------------------

init_library(core)
target_compile_definitions(core PUBLIC PROGRAM_NAME="${PROGRAM_NAME}" PROJECT_NAME="${PROJECT_NAME}")
target_sources(core PRIVATE
    src/core/log.cpp
    src/core/object.cpp
    src/core/fd.cpp
    src/core/stacktrace.cpp
    src/core/process.cpp
    src/core/spawn.cpp
    src/core/spawn.${CMAKE_SYSTEM_PROCESSOR}.S
    src/core/debug.cpp
    src/core/id.cpp
    src/core/exec.cpp
    src/core/timer.cpp
    )
target_include_directories(core PUBLIC
    src
    ${BUILD_DIR}/math
    )
target_link_libraries(core PUBLIC
    PkgConfig::wayland-server
    PkgConfig::wayland-client
    PkgConfig::xkbcommon
    PkgConfig::libevdev
    PkgConfig::libinput
    PkgConfig::xcursor
    PkgConfig::libdrm
    PkgConfig::libudev
    PkgConfig::libseat
    PkgConfig::libcap
    PkgConfig::freetype

    vendor::stb-image
    vendor::renderdoc-api
    vendor::vma-object

    Vulkan::Headers
    unordered_dense::unordered_dense
    generated::wayland

    stdc++exp
    )

# ------------------------------------------------------------------------------
#       GPU
# ------------------------------------------------------------------------------

add_subdirectory(${BUILD_DIR}/shaders)

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
    ${BUILD_DIR}/formats/formats.cpp
    )
target_link_libraries(gpu PUBLIC core generated::shaders)

# ------------------------------------------------------------------------------
#       Scene
# ------------------------------------------------------------------------------

init_library(scene)
target_sources(scene PRIVATE
    src/scene/render.cpp
    src/scene/compute.cpp
    src/scene/node/node.cpp
    src/scene/node/texture.cpp
    src/scene/node/tree.cpp
    src/scene/node/input-region.cpp
    )
target_link_libraries(scene PUBLIC core gpu)

# ------------------------------------------------------------------------------
#       Seat
# ------------------------------------------------------------------------------

init_library(seat)
target_sources(seat PRIVATE
    src/seat/keyboard.cpp
    src/seat/pointer.cpp
    src/seat/seat.cpp
    src/seat/cursor.cpp
    src/seat/client.cpp
    src/seat/data.cpp
    src/seat/focus.cpp
    )
target_link_libraries(seat PUBLIC core scene)

# ------------------------------------------------------------------------------
#       UI
# ------------------------------------------------------------------------------

init_library(ui)
target_sources(ui PRIVATE
    src/ui/font.cpp
    src/ui/string.cpp
    )
target_link_libraries(ui PUBLIC core scene seat)

# ------------------------------------------------------------------------------
#       Window Manager
# ------------------------------------------------------------------------------

init_library(wm)
target_sources(wm PRIVATE
    src/wm/wm.cpp
    src/wm/client.cpp
    src/wm/io.cpp
    src/wm/render.cpp
    src/wm/seat.cpp
    src/wm/cursor.cpp
    src/wm/window.cpp
    src/wm/toast.cpp
    src/wm/movesize.cpp
    src/wm/hotkeys.cpp
    src/wm/zone.cpp
    src/wm/selection.cpp
    src/wm/focus.cpp
    src/wm/pointer-constraints.cpp
    )
target_link_libraries(wm PUBLIC core scene seat)

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
target_link_libraries(io PUBLIC wm)

# ------------------------------------------------------------------------------
#       Wayland
# ------------------------------------------------------------------------------

init_library(way)
target_sources(way PRIVATE
    src/way/client.cpp
    src/way/server.cpp
    src/way/util.cpp

    src/way/buffer/syncobj.cpp
    src/way/buffer/buffer.cpp
    src/way/buffer/dmabuf.cpp
    src/way/buffer/shm.cpp

    src/way/data/data.cpp

    src/way/output/output.cpp

    src/way/seat/cursor-shape.cpp
    src/way/seat/gestures.cpp
    src/way/seat/seat.cpp
    src/way/seat/keyboard.cpp
    src/way/seat/pointer.cpp
    src/way/seat/relative-pointer.cpp
    src/way/seat/pointer-constraints.cpp

    src/way/shell/popup.cpp
    src/way/shell/shell.cpp
    src/way/shell/activation.cpp

    src/way/surface/decoration.cpp
    src/way/surface/subsurface.cpp
    src/way/surface/surface.cpp
    src/way/surface/viewport.cpp
    )
target_link_libraries(way PUBLIC wm)

# ------------------------------------------------------------------------------
#       Shell
# ------------------------------------------------------------------------------

init_executable(${PROGRAM_NAME})
target_sources(${PROGRAM_NAME} PRIVATE
    src/shell/main.cpp
    src/shell/xwayland.cpp
    src/shell/hotkeys.cpp
    src/shell/background.cpp
    )
target_link_libraries(${PROGRAM_NAME} PUBLIC wm io way ui)
