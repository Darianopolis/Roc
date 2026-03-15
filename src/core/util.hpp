#pragma once

#include "pch.hpp"
#include "log.hpp"
#include "types.hpp"

// -----------------------------------------------------------------------------

namespace core
{
    template<typename Fn>
    struct DeferGuard
    {
        Fn fn;

        DeferGuard(Fn&& fn): fn(std::move(fn)) {}
        ~DeferGuard() { fn(); };
    };
}

#define defer ::core::DeferGuard _ = [&]

// -----------------------------------------------------------------------------

namespace core
{
    template<typename... Ts>
    struct OverloadSet : Ts... {
        using Ts::operator()...;
    };

    template<typename... Ts> OverloadSet(Ts...) -> OverloadSet<Ts...>;
}

// -----------------------------------------------------------------------------

namespace core
{
    constexpr auto ptr_to(auto&& value) { return &value; }
}

// -----------------------------------------------------------------------------

#define CORE_DELETE_COPY(Type) \
               Type(const Type& ) = delete; \
    Type& operator=(const Type& ) = delete; \

#define CORE_DELETE_COPY_MOVE(Type) \
    CORE_DELETE_COPY(Type) \
               Type(Type&&) = delete; \
    Type& operator=(Type&&) = delete;

// -----------------------------------------------------------------------------

namespace core
{
    constexpr usz round_up_power2(usz v) noexcept
    {
        v--;
        v |= v >> 1;
        v |= v >> 2;
        v |= v >> 4;
        v |= v >> 8;
        v |= v >> 16;
        v |= v >> 32;
        v++;

        return v;
    }
}
