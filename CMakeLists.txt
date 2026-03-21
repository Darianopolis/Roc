cmake_minimum_required(VERSION 4.0)

project(roc)

# ------------------------------------------------------------------------------

function(target_generated_source target visibility generated_source_name)
    string(JOIN "\n" generated_content ${ARGN})
    file(GENERATE
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${generated_source_name}"
        CONTENT "${generated_content}")
    target_sources(${target} ${visibility} "${CMAKE_CURRENT_BINARY_DIR}/${generated_source_name}")
endfunction()

# ------------------------------------------------------------------------------

add_subdirectory(${VENDOR_DIR}/wayland EXCLUDE_FROM_ALL)

# ------------------------------------------------------------------------------

add_library(               libevdev INTERFACE)
target_link_libraries(     libevdev INTERFACE evdev)
target_include_directories(libevdev INTERFACE /usr/include/libevdev-1.0)

# ------------------------------------------------------------------------------

# TODO: Helper function for pkg config dependencies
add_library(               libgio2 INTERFACE)
target_include_directories(libgio2 INTERFACE /usr/include/glib-2.0
                                             /usr/include/gio-unix-2.0
                                             /usr/lib/glib-2.0/include)
target_link_libraries(     libgio2 INTERFACE /usr/lib/libgio-2.0.so
                                             /usr/lib/libglib-2.0.so
                                             /usr/lib/libgobject-2.0.so)

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
target_link_libraries(     vma-object PUBLIC GPUOpen::VulkanMemoryAllocator)
target_compile_definitions(vma-object PUBLIC "VMA_STATIC_VULKAN_FUNCTIONS=0" "VMA_DYNAMIC_VULKAN_FUNCTIONS=1")

# ------------------------------------------------------------------------------

add_library(renderdoc-api INTERFACE)
target_include_directories(renderdoc-api INTERFACE ${VENDOR_DIR}/renderdoc/renderdoc/api/app)

# ------------------------------------------------------------------------------

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

init_library(core)
target_precompile_headers(core PUBLIC src/core/pch.hpp)
target_compile_definitions(core PUBLIC PROGRAM_NAME="roc" PROJECT_NAME="Roc")
target_sources(core PUBLIC
    src/core/log.cpp
    src/core/string.cpp
    src/core/object.cpp
    src/core/fd.cpp
    src/core/stacktrace.cpp
    src/core/process.cpp
    src/core/chrono.cpp
    src/core/debug.cpp
    src/core/memory.cpp

    src/exec/exec.cpp
    )
target_include_directories(core PUBLIC
    src
    /usr/include/drm
    )
target_link_libraries(core PUBLIC
    wayland-server
    wayland-client
    wayland-header
    xkbcommon
    libevdev
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
    drm
    udev
    seat
    imgui
    cap
    libgio2
    renderdoc-api
    )

# ------------------------------------------------------------------------------

add_subdirectory(.build/shaders)

init_library(gpu)
target_sources(gpu PUBLIC
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

init_library(io)
target_sources(io PUBLIC
    src/io/io.cpp
    src/io/output.cpp
    src/io/input.cpp
    src/io/drm/drm.cpp
    src/io/evdev/evdev.cpp
    src/io/libinput/libinput.cpp
    src/io/session/session.cpp
    src/io/udev/udev.cpp
    src/io/wayland/wayland.cpp
    src/io/wayland/output.cpp
    src/io/wayland/seat.cpp
    )
target_link_libraries(io PUBLIC core gpu)

init_executable(io-test)
target_sources(io-test PUBLIC src/io/main.cpp)
target_link_libraries(io-test PUBLIC io)

# ------------------------------------------------------------------------------

init_library(scene)
target_sources(scene PUBLIC
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

init_library(ui)
target_sources(ui PUBLIC
    src/ui/ui.cpp
    )
target_link_libraries(ui PUBLIC scene)

# ------------------------------------------------------------------------------

init_library(way)
target_sources(way PUBLIC
    src/way/way.cpp
    src/way/buffer.cpp
    src/way/surface.cpp
    src/way/subsurface.cpp
    src/way/shell.cpp
    src/way/popup.cpp
    src/way/shm.cpp
    src/way/dmabuf.cpp
    src/way/client.cpp
    src/way/seat.cpp
    src/way/output.cpp
    src/way/data.cpp
    src/way/viewport.cpp
    src/way/gestures.cpp
    src/way/cursor-shape.cpp
    src/way/decoration.cpp
    )
target_link_libraries(way PUBLIC scene)

# ------------------------------------------------------------------------------

init_executable(roc)
target_sources(roc PUBLIC
    src/wm/main.cpp
    src/wm/wm.cpp
    src/wm/movesize.cpp
    )
target_link_libraries(roc PUBLIC scene way ui)
