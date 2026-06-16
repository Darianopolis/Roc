#pragma once

#include <core/object.hpp>
#include <core/exec.hpp>
#include <core/signal.hpp>

#include <gpu/gpu.hpp>

#include <wm/wm.hpp>

// -----------------------------------------------------------------------------

struct IoContext;

// -----------------------------------------------------------------------------

auto io_create(WmServer*, ExecContext*, Gpu*) -> Ref<IoContext>;

struct IoSignals
{
    Signal<void()> shutdown;
};

auto io_get_signals(IoContext*) -> IoSignals&;
void io_start(IoContext*);
void io_stop(IoContext*);

void io_output_create(IoContext*);
