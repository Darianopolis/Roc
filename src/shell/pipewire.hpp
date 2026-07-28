#include "shell.hpp"

#include <core/timer.hpp>

#include <pipewire/pipewire.h>
#include <spa/param/video/format-utils.h>
#include <spa/param/format-utils.h>
#include <spa/param/buffers.h>
#include <spa/pod/builder.h>
#include <spa/pod/parser.h>
#include <spa/pod/dynamic.h>
#include <spa/buffer/buffer.h>
#include <spa/buffer/meta.h>
#include <spa/utils/result.h>

struct ShellPwStream;

struct ShellPwContext : ShellPlugin
{
    pw_loop*    loop;
    pw_context* context;
    pw_core*    core;
    spa_hook    core_hook;

    Shell* shell;
    Ref<GpuImagePool> pool;

    Ref<ShellPwStream> stream;

    Listener<void()> output_layout_listener;

    ~ShellPwContext();
};

struct ShellPwStream
{
    ShellPwContext* ctx;

    pw_stream* stream;
    spa_hook   stream_hook;

    GpuFormat format;
    GpuDrmModifier modifier;
    vec2u32 extent;
    rect2f32 viewport;

    Ref<GpuImage> last_image;

    pw_stream_state state;
    bool enabled = false;

    Listener<void()> frame_listener;

    ~ShellPwStream();
};

struct ShellPwBuffer
{
    ShellPwStream* stream;

    Fd fd;
    void* mapped;
    Ref<GpuImage> dmabuf;

    ~ShellPwBuffer();
};

auto shell_pw_find_plugin(Shell*) -> ShellPwContext*;
