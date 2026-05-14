#pragma once

// -----------------------------------------------------------------------------

#if defined(__clang__)
#define CORE_NOINLINE [[clang::noinline]]

#elif defined(__GNUC__) || defined(__GNUG__)
#define CORE_NOINLINE __attribute__ ((noinline))
#endif

// -----------------------------------------------------------------------------

#include <thread>
#include <print>
#include <vector>
#include <iostream>
#include <fstream>
#include <format>
#include <string>
#include <typeinfo>
#include <span>
#include <string_view>
#include <optional>
#include <filesystem>
#include <variant>
#include <bit>
#include <algorithm>
#include <source_location>
#include <list>
#include <ranges>
#include <random>
#include <stacktrace>
#include <flat_set>
#include <flat_map>
#include <mutex>
#include <deque>
#include <meta>
#include <debugging>

#include <cstring>
#include <csignal>

// -----------------------------------------------------------------------------

#include <unistd.h>
#include <stdarg.h>
#include <fcntl.h>
#include <dlfcn.h>

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/poll.h>
#include <sys/eventfd.h>
#include <sys/capability.h>
#include <sys/prctl.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/syscall.h>
#include <sys/timerfd.h>

#include <drm/drm_fourcc.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include <linux/dma-buf.h>

// -----------------------------------------------------------------------------

#include <libinput.h>

// -----------------------------------------------------------------------------

extern "C" {
    #include <libseat.h>
}

// -----------------------------------------------------------------------------

#include <gio/gio.h>
#include <gio/gdesktopappinfo.h>

// -----------------------------------------------------------------------------

#include <X11/Xcursor/Xcursor.h>

// -----------------------------------------------------------------------------

#include <xkbcommon/xkbcommon.h>
#include <libevdev/libevdev.h>

// -----------------------------------------------------------------------------

#include <ankerl/unordered_dense.h>

// -----------------------------------------------------------------------------

#ifndef VK_NO_PROTOTYPES
# define VK_NO_PROTOTYPES
#endif

#include <vulkan/vulkan.h>

#include <vk_mem_alloc.h>

// -----------------------------------------------------------------------------

#include "renderdoc_app.h"

// -----------------------------------------------------------------------------

#include <stb_image.h>
#include <stb_image_write.h>

// -----------------------------------------------------------------------------

using namespace std::literals;
