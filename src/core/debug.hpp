#pragma once

CORE_NOINLINE inline
void core_debugbreak()
{
    std::cerr << std::stacktrace::current() << std::endl;
    raise(SIGTRAP);
}

[[noreturn]] CORE_NOINLINE inline
void core_debugkill()
{
    std::cerr << std::stacktrace::current() << std::endl;
    std::terminate();
}

[[noreturn]] inline
void core_unreachable()
{
#ifdef NDEBUG
    std::unreachable();
#else
    core_debugkill();
#endif
}

[[noreturn]] CORE_NOINLINE
void core_assert_fail(std::string_view expr, std::string_view reason = {});

#define core_assert(Expr, ...) \
    (static_cast<bool>(Expr) ? void() : core_assert_fail(#Expr __VA_OPT__(, std::format(__VA_ARGS__))))

// -----------------------------------------------------------------------------
//      Error Checking
// -----------------------------------------------------------------------------

void core_log_unix_error(std::string_view message, int err = 0);

template<typename T>
struct core_unix_result
{
    T   value;
    int error;

    bool ok()  const noexcept { return !error; }
    bool err() const noexcept { return  error; }
};

enum class unix_error_behaviour
{
    negative_one,
    negative_errno,
    positive_errno,
    check_errno,
    null,
};

template<auto Function>
struct unix_error_behaviour_helper { static_assert(false); };

template<auto Function, int... Quiet>
auto unix_check(auto... args) -> core_unix_result<decltype(Function(args...))>
{
    static constexpr auto behaviour = unix_error_behaviour_helper<Function>::behaviour;

    if constexpr (behaviour == unix_error_behaviour::check_errno) {
        errno = 0;
    }

    auto res = Function(args...);

    int err;
    if constexpr (behaviour == unix_error_behaviour::negative_one) {
        if (res != decltype(res)(-1)) [[likely]] return { res };
        err = errno;

    } else if constexpr (behaviour == unix_error_behaviour::negative_errno) {
        if (res >= 0) [[likely]] return { res };
        err = -res;

    } else if constexpr (behaviour == unix_error_behaviour::positive_errno) {
        if (res <= 0) [[likely]] return { res };
        err = res;

    } else if constexpr (behaviour == unix_error_behaviour::null) {
        if (res) [[likely]] return { res };
        err = errno;

    } else if constexpr (behaviour == unix_error_behaviour::check_errno) {
        if (!errno) [[likely]] return { res };
        err = errno;
    }

    if (!(... || (err == Quiet))) core_log_unix_error("unix_check", err);
    return { res, err };
}

#define CORE_UNIX_ERROR_BEHAVIOUR(Function, Behaviour) \
    template<> struct unix_error_behaviour_helper<Function> { \
        static constexpr auto behaviour = unix_error_behaviour::Behaviour; \
    };


#include "unix-check.inl"
