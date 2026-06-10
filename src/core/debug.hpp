#pragma once

#include "pch.hpp"

void debug_handlers();

CORE_NOINLINE inline
void debug_break()
{
    std::breakpoint();
}

[[noreturn]] CORE_NOINLINE inline
void debug_kill()
{
    std::terminate();
}

[[noreturn]] inline
void debug_unreachable()
{
    debug_kill();
}

[[noreturn]] CORE_NOINLINE
void debug_assert_fail(std::string_view expr, std::string_view reason = {});

#define debug_assert(Expr, ...) \
    (static_cast<bool>(Expr) ? void() : debug_assert_fail(#Expr __VA_OPT__(, std::format(__VA_ARGS__))))

// -----------------------------------------------------------------------------
//      Error Checking
// -----------------------------------------------------------------------------

using unix_error_t = int;

void log_unix_error(std::string_view message, unix_error_t err = 0);

template<typename T>
struct UnixResult
{
    T            value;
    unix_error_t error;

    auto ok()  const noexcept -> bool { return !error; }
    auto err() const noexcept -> bool { return  error; }
};

enum class UnixErrorBehavior
{
    negative_one,
    negative_errno,
    positive_errno,
    check_errno,
    null,
};

template<auto Function>
struct UnixErrorBehaviorHelper { static_assert(false); };

template<auto Function, unix_error_t... Quiet>
auto unix_check(auto... args) -> UnixResult<decltype(Function(args...))>
{
    static constexpr auto behaviour = UnixErrorBehaviorHelper<Function>::behaviour;

    if constexpr (behaviour == UnixErrorBehavior::check_errno) {
        errno = 0;
    }

    auto res = Function(args...);

    unix_error_t err;
    if constexpr (behaviour == UnixErrorBehavior::negative_one) {
        if (res != decltype(res)(-1)) [[likely]] return { res };
        err = errno;

    } else if constexpr (behaviour == UnixErrorBehavior::negative_errno) {
        if (res >= 0) [[likely]] return { res };
        err = -res;

    } else if constexpr (behaviour == UnixErrorBehavior::positive_errno) {
        if (res <= 0) [[likely]] return { res };
        err = res;

    } else if constexpr (behaviour == UnixErrorBehavior::null) {
        if (res) [[likely]] return { res };
        err = errno;

    } else if constexpr (behaviour == UnixErrorBehavior::check_errno) {
        if (!errno) [[likely]] return { res };
        err = errno;
    }

    if (!(... || (err == Quiet))) log_unix_error(__PRETTY_FUNCTION__, err);
    return { res, err };
}

#define UNIX_ERROR_BEHAVIOUR(Function, Behaviour) \
    template<> struct UnixErrorBehaviorHelper<Function> { \
        static constexpr auto behaviour = UnixErrorBehavior::Behaviour; \
    };


#include "debug-checks.inl"
