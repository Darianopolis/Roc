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

struct ShellPwContext
{
    Gpu* gpu;
    ExecContext* exec;

    pw_loop*    loop;
    pw_context* context;
    pw_core*    core;
    spa_hook    core_hook;

    ~ShellPwContext();
};

struct ShellPwStream
{
    Ref<ShellPwContext> ctx;

    pw_stream* stream;
    spa_hook   stream_hook;

    GpuFormat format;
    GpuDrmModifier modifier;
    vec2u32 extent;

    pw_stream_state state;

    struct {
        Signal<void(pw_stream_state)> state_changed;
    } signals;

    ~ShellPwStream();
};

struct ShellPwBuffer
{
    GpuFormat format;
    vec2u32 extent;

    pw_buffer* buffer;

    Fd fd;
    void* mapped;
    Ref<GpuImage> dmabuf;
    u32 stride;

    ~ShellPwBuffer();
};

auto shell_pw_context_create(ExecContext*, Gpu*) -> Ref<ShellPwContext>;
auto shell_pw_stream_create(ShellPwContext*, vec2u32 extent) -> Ref<ShellPwStream>;

auto shell_pw_stream_dequeue(ShellPwStream*) -> ShellPwBuffer*;
void shell_pw_stream_enqueue(ShellPwStream*, ShellPwBuffer*);
