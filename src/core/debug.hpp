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

#define UNIX_ERROR_BEHAVIOR_SIMPLE(ErrorExpr, ...) \
    []<auto Function>(auto... args) -> UnixResult<decltype(Function(args...))> { \
        __VA_ARGS__; \
        auto res = Function(args...); \
        return { res, ErrorExpr }; \
    }

namespace UnixErrorBehavior
{
    static constexpr auto negative_errno = UNIX_ERROR_BEHAVIOR_SIMPLE(  res  < decltype(res)( 0) ? unix_error_t(-res) : 0);
    static constexpr auto positive_errno = UNIX_ERROR_BEHAVIOR_SIMPLE(  res  > decltype(res)( 0) ? unix_error_t( res) : 0);
    static constexpr auto negative       = UNIX_ERROR_BEHAVIOR_SIMPLE(  res  < decltype(res)( 0) ? errno              : 0);
    static constexpr auto negative_one   = UNIX_ERROR_BEHAVIOR_SIMPLE(  res == decltype(res)(-1) ? errno              : 0);
    static constexpr auto null           = UNIX_ERROR_BEHAVIOR_SIMPLE( !res                      ? errno              : 0);
    static constexpr auto non_zero       = UNIX_ERROR_BEHAVIOR_SIMPLE(  res                      ? errno              : 0);
    static constexpr auto check_errno    = UNIX_ERROR_BEHAVIOR_SIMPLE(errno, errno = 0);
};

template<auto Function>
struct UnixFunction { static_assert(false); };

template<auto Function, unix_error_t... Quiet>
auto unix_check(auto... args) -> UnixResult<decltype(Function(args...))>
{
    auto res = UnixFunction<Function>::behavior.template operator()<Function>(args...);
    if (res.ok()) return res;
    if (!(... || (res.error == Quiet))) log_unix_error(UnixFunction<Function>::function_name, res.error);
    return res;
}

#define UNIX_FUNCTION(Function, Behavior) \
    template<> struct UnixFunction<Function> { \
        static constexpr auto behavior = Behavior; \
        static constexpr auto function_name = #Function; \
    };

#include "debug-checks.inl"
