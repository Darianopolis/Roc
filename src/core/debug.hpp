#pragma once

namespace core
{
    CORE_NOINLINE inline
    void debugbreak()
    {
        std::cerr << std::stacktrace::current() << std::endl;
        raise(SIGTRAP);
    }

    [[noreturn]] CORE_NOINLINE inline
    void debugkill()
    {
        std::cerr << std::stacktrace::current() << std::endl;
        std::terminate();
    }

    [[noreturn]] inline
    void unreachable()
    {
    #ifdef NDEBUG
        std::unreachable();
    #else
        core::debugkill();
    #endif
    }

    [[noreturn]] CORE_NOINLINE
    void assert_fail(std::string_view expr, std::string_view reason = {});
}

#define core_assert(Expr, ...) \
    (static_cast<bool>(Expr) ? void() : core::assert_fail(#Expr __VA_OPT__(, std::format(__VA_ARGS__))))

// -----------------------------------------------------------------------------
//      Error Checking
// -----------------------------------------------------------------------------

namespace core
{
    void log_unix_error(std::string_view message, int err = 0);

    template<typename T>
    struct UnixResult
    {
        T   value;
        int error;

        bool ok()  const noexcept { return !error; }
        bool err() const noexcept { return  error; }
    };

    enum class ErrorBehaviour
    {
        negative_one,
        negative_errno,
        positive_errno,
        check_errno,
        null,
    };

    template<auto Function>
    struct error_behaviour { static_assert(false); };

    template<auto Function, int... Quiet>
    auto check(auto... args) -> core::UnixResult<decltype(Function(args...))>
    {
        static constexpr auto behaviour = core::error_behaviour<Function>::behaviour;

        if constexpr (behaviour == core::ErrorBehaviour::check_errno) {
            errno = 0;
        }

        auto res = Function(args...);

        int err;
        if constexpr (behaviour == core::ErrorBehaviour::negative_one) {
            if (res != decltype(res)(-1)) [[likely]] return { res };
            err = errno;

        } else if constexpr (behaviour == core::ErrorBehaviour::negative_errno) {
            if (res >= 0) [[likely]] return { res };
            err = -res;

        } else if constexpr (behaviour == core::ErrorBehaviour::positive_errno) {
            if (res <= 0) [[likely]] return { res };
            err = res;

        } else if constexpr (behaviour == core::ErrorBehaviour::null) {
            if (res) [[likely]] return { res };
            err = errno;

        } else if constexpr (behaviour == core::ErrorBehaviour::check_errno) {
            if (!errno) [[likely]] return { res };
            err = errno;
        }

        if (!(... || (err == Quiet))) core::log_unix_error("core::check", err);
        return { res, err };
    }
}

#define CORE_UNIX_ERROR_BEHAVIOUR(Function, Behaviour) \
    template<> struct core::error_behaviour<Function> { \
        static constexpr auto behaviour = core::ErrorBehaviour::Behaviour; \
    };

#include "unix-check.inl"
