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
pkg_check_modules(libsystemd     REQUIRED IMPORTED_TARGET libsystemd)
pkg_check_modules(pipewire       REQUIRED IMPORTED_TARGET libpipewire-0.3)

# ------------------------------------------------------------------------------
#       Git Dependencies
# ------------------------------------------------------------------------------

add_subdirectory(${BUILD_DIR}/wayland EXCLUDE_FROM_ALL)

# ------------------------------------------------------------------------------

add_library(stb-image OBJECT)
target_include_directories(stb-image PUBLIC ${VENDOR_DIR}/stb)
target_generated_source(stb-image PRIVATE stb_image.c       "#define STB_IMAGE_IMPLEMENTATION 1"       "#include <stb_image.h>")
target_generated_source(stb-image PRIVATE stb_image_write.c "#define STB_IMAGE_WRITE_IMPLEMENTATION 1" "#include <stb_image_write.h>")
add_library(vendor::stb-image ALIAS stb-image)

# ------------------------------------------------------------------------------

add_subdirectory(${VENDOR_DIR}/unordered-dense EXCLUDE_FROM_ALL)

# ------------------------------------------------------------------------------

add_subdirectory(${VENDOR_DIR}/vulkan-headers EXCLUDE_FROM_ALL)

# ------------------------------------------------------------------------------

add_library(renderdoc-api INTERFACE)
target_include_directories(renderdoc-api INTERFACE ${VENDOR_DIR}/renderdoc/renderdoc/api/app)
add_library(vendor::renderdoc-api ALIAS renderdoc-api)

# ------------------------------------------------------------------------------
#       Library
# ------------------------------------------------------------------------------

set(LIBNAME "lib${PROGRAM_NAME}")
add_library(${LIBNAME})

set_target_properties(${LIBNAME} PROPERTIES PREFIX "")

target_precompile_headers(${LIBNAME} PRIVATE src/core/pch.hpp)

target_compile_options(${LIBNAME} PRIVATE
    -std=c++26

    -Wall
    -Wextra
    -Werror
    -Wimplicit-fallthrough
    -Wconversion

    -Wno-missing-field-initializers
    -Wno-unknown-pragmas
    -Wno-unused-parameter
    -Wno-invalid-offsetof
    )

if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(${LIBNAME} PRIVATE
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
    target_compile_options(${LIBNAME} PRIVATE
        -freflection

        -Wno-changes-meaning # we use `foo* foo` a lot in wayland protocol bindings
        -Wno-maybe-uninitialized
        )
endif()

if(USE_ASAN)
    target_compile_options(${LIBNAME} PUBLIC -fsanitize=address)
    target_link_options(   ${LIBNAME} PUBLIC -fsanitize=address)
endif()

target_compile_definitions(${LIBNAME} PUBLIC PROGRAM_NAME="${PROGRAM_NAME}" PROJECT_NAME="${PROJECT_NAME}")

# ------------------------------------------------------------------------------
#       Core
# ------------------------------------------------------------------------------

target_sources(${LIBNAME} PRIVATE
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

target_include_directories(${LIBNAME} PUBLIC
    src
    ${BUILD_DIR}/math
    )

target_link_libraries(${LIBNAME} PUBLIC
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
    PkgConfig::libsystemd
    PkgConfig::pipewire

    vendor::stb-image
    vendor::renderdoc-api

    unordered_dense::unordered_dense
    generated::wayland

    stdc++exp
    )

# ------------------------------------------------------------------------------
#       GPU
# ------------------------------------------------------------------------------

add_subdirectory(${BUILD_DIR}/shaders)

target_sources(${LIBNAME} PRIVATE
    src/gpu/gpu.cpp
    src/gpu/functions.cpp
    src/gpu/buffer.cpp
    src/gpu/sampler.cpp
    src/gpu/image.cpp
    src/gpu/descriptors.cpp
    src/gpu/formats.cpp
    src/gpu/pipeline.cpp
    src/gpu/commands.cpp
    src/gpu/semaphore.cpp
    src/gpu/image-pool.cpp
    ${BUILD_DIR}/formats/formats.cpp
    )

target_link_libraries(${LIBNAME} PUBLIC
    generated::shaders
    Vulkan::Headers
    )

# ------------------------------------------------------------------------------
#       Scene
# ------------------------------------------------------------------------------

target_sources(${LIBNAME} PRIVATE
    src/scene/render.cpp
    src/scene/node/node.cpp
    src/scene/node/texture.cpp
    src/scene/node/tree.cpp
    src/scene/node/input-region.cpp
    )

# ------------------------------------------------------------------------------
#       Seat
# ------------------------------------------------------------------------------

target_sources(${LIBNAME} PRIVATE
    src/seat/keyboard.cpp
    src/seat/pointer.cpp
    src/seat/seat.cpp
    src/seat/cursor.cpp
    src/seat/client.cpp
    src/seat/data.cpp
    src/seat/focus.cpp
    )

# ------------------------------------------------------------------------------
#       UI
# ------------------------------------------------------------------------------

target_sources(${LIBNAME} PRIVATE
    src/ui/font.cpp
    src/ui/string.cpp
    )

# ------------------------------------------------------------------------------
#       Window Manager
# ------------------------------------------------------------------------------

target_sources(${LIBNAME} PRIVATE
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

# ------------------------------------------------------------------------------
#       Session IO
# ------------------------------------------------------------------------------

target_sources(${LIBNAME} PRIVATE
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

# ------------------------------------------------------------------------------
#       Wayland
# ------------------------------------------------------------------------------

target_sources(${LIBNAME} PRIVATE
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

# ------------------------------------------------------------------------------
#       Shell
# ------------------------------------------------------------------------------

target_sources(${LIBNAME} PRIVATE
    src/shell/main.cpp
    src/shell/xwayland.cpp
    src/shell/hotkeys.cpp
    src/shell/background.cpp
    src/shell/pipewire.cpp
    src/shell/screencast.cpp
    )

# ------------------------------------------------------------------------------
#       Entry
# ------------------------------------------------------------------------------

add_executable(       ${PROGRAM_NAME})
target_sources(       ${PROGRAM_NAME} PRIVATE src/shell/entry.cpp)
target_link_libraries(${PROGRAM_NAME} PUBLIC ${LIBNAME})
