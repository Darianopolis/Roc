#pragma once

#include "object.hpp"
#include "debug.hpp"
#include "fd.hpp"
#include "util.hpp"

namespace detail
{
    template<auto Main>
    static
    auto main(int argc, char* argv[]) -> int
    {
        debug_init();
        allocator_init();
        fd_registry_init();
        fd_mark_open_as_inherited();
        defer {
            fd_registry_deinit();
            allocator_deinit();
        };
        return Main(argc, argv);
    }
}

#define DEFINE_MAIN(Main) \
    auto main(int argc, char* argv[]) -> int \
    { \
        return detail::main<Main>(argc, argv); \
    }
