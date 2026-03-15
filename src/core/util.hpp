#pragma once

#include "pch.hpp"
#include "log.hpp"
#include "types.hpp"

// -----------------------------------------------------------------------------

template<typename Fn>
struct core_defer_guard
{
    Fn fn;

    core_defer_guard(Fn&& fn): fn(std::move(fn)) {}
    ~core_defer_guard() { fn(); };
};

#define defer core_defer_guard _ = [&]

// -----------------------------------------------------------------------------

template<typename... Ts>
struct core_overload_set : Ts... {
    using Ts::operator()...;
};

template<typename... Ts> core_overload_set(Ts...) -> core_overload_set<Ts...>;

// -----------------------------------------------------------------------------

constexpr auto ptr_to(auto&& value) { return &value; }

// -----------------------------------------------------------------------------

#define CORE_DELETE_COPY(Type) \
               Type(const Type& ) = delete; \
    Type& operator=(const Type& ) = delete; \

#define CORE_DELETE_COPY_MOVE(Type)         \
    CORE_DELETE_COPY(Type)                  \
               Type(      Type&&) = delete; \
    Type& operator=(      Type&&) = delete;

// -----------------------------------------------------------------------------

constexpr usz core_round_up_power2(usz v) noexcept
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
