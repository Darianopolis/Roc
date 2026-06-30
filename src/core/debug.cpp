#include "debug.hpp"
#include "log.hpp"

[[noreturn]] CORE_NOINLINE
void debug_assert_fail(std::string_view expr, std::string_view reason)
{
    log_error("assert({}) failed{}{}", expr, reason.empty() ? "" : ": ", reason);
    debug_kill();
}

void log_unix_error(std::string_view message, unix_error_t err)
{
    err = err ?: errno;

    if (message.empty()) { log_error("({}) {}",              err, strerror(err)); }
    else                 { log_error("{}: ({}) {}", message, err, strerror(err)); }
}

#define HANDLED_POSIX_SIGNALS(DO) \
    DO(SIGABRT) /* Abort signal from abort(3) */ \
    DO(SIGBUS)  /* Bus error (bad memory access) */ \
    DO(SIGFPE)  /* Floating point exception */ \
    DO(SIGILL)  /* Illegal Instruction */ \
    DO(SIGQUIT) /* Quit from keyboard */ \
    DO(SIGSEGV) /* Invalid memory reference */ \
    DO(SIGSYS)  /* Bad argument to routine (SVr4) */ \
    DO(SIGTRAP) /* Trace/breakpoint trap */ \
    DO(SIGXCPU) /* CPU time limit exceeded (4.2BSD) */ \
    DO(SIGXFSZ) /* File size limit exceeded (4.2BSD) */ \

constexpr int posix_signals[] = {
#define DO(name) name,
    HANDLED_POSIX_SIGNALS(DO)
#undef DO
};

constexpr auto posix_signal_names = [] {
    std::array<std::string_view, std::ranges::max(posix_signals) + 1> names = {};
#define DO(name) names[name] = #name;
    HANDLED_POSIX_SIGNALS(DO)
#undef DO
    return names;
}();

static
void handle_signal(int signal, siginfo_t*, void*)
{
    std::println(stderr, "{} ({})", posix_signal_names[signal], signal);
    std::println(stderr, "{}", std::stacktrace::current());
    fflush(stderr);
}

void debug_handlers()
{
    for (int signal : posix_signals) {
        struct sigaction action;
        memset(&action, 0, sizeof(action));
        action.sa_flags = static_cast<int>(SA_SIGINFO | SA_ONSTACK | SA_NODEFER | SA_RESETHAND);
        sigfillset(&action.sa_mask);
        sigdelset(&action.sa_mask, signal);
        action.sa_sigaction = &handle_signal;
        sigaction(signal, &action, nullptr);
    }
}
