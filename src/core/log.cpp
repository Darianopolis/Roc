#include "pch.hpp"
#include "log.hpp"
#include "stacktrace.hpp"
#include "chrono.hpp"
#include "enum.hpp"

#define VT_COLOR_BEGIN(color) "\u001B[" #color "m"
#define VT_COLOR_RESET "\u001B[0m"
#define VT_COLOR(color, text) VT_COLOR_BEGIN(color) text VT_COLOR_RESET

struct LogState
{
    std::ofstream log_file;
    bool is_stderr_redirected;

    StacktraceCache stacktraces;
    std::recursive_mutex mutex;
};

static
auto get_log_state() -> LogState&
{
    static LogState state;
    return state;
}

void log_set_structured_log(const std::filesystem::path& path)
{
    auto& state = get_log_state();
    std::scoped_lock _ { state.mutex };

    state.log_file = std::ofstream(path);
}

void log_redirect_stdout(const std::filesystem::path& path)
{
    unix_check<freopen>(path.c_str(), "w", stdout);
}

void log_redirect_stderr(const std::filesystem::path& path)
{
    auto& state = get_log_state();
    std::scoped_lock _ { state.mutex };

    state.is_stderr_redirected = true;
    unix_check<freopen>(path.c_str(), "w", stderr);
}

void log(LogSemantic semantic, std::string_view message)
{
    // Strip trailing newlines
    while (message.ends_with('\n')) message.remove_suffix(1);

    auto timestamp = time_current();

    auto& state = get_log_state();
    std::scoped_lock _ { state.mutex };

    const char* format;
    if (state.is_stderr_redirected) {
        switch (semantic) {
            break;case LogSemantic::trace: format = "{} [TRACE] {}\n";
            break;case LogSemantic::debug: format = "{} [DEBUG] {}\n";
            break;case LogSemantic::info:  format = "{}  [INFO] {}\n";
            break;case LogSemantic::warn:  format = "{}  [WARN] {}\n";
            break;case LogSemantic::error: format = "{} [ERROR] {}\n";
            break;case LogSemantic::fatal: format = "{} [FATAL] {}\n";
        }
    } else {
        switch (semantic) {
            break;case LogSemantic::trace: format = VT_COLOR(90, "{}") " ["  VT_COLOR(90, "TRACE") "] " VT_COLOR(90, "{}") "\n";
            break;case LogSemantic::debug: format = VT_COLOR(90, "{}") " ["  VT_COLOR(96, "DEBUG") "] "              "{}"  "\n";
            break;case LogSemantic::info:  format = VT_COLOR(90, "{}") "  [" VT_COLOR(94,  "INFO") "] "              "{}"  "\n";
            break;case LogSemantic::warn:  format = VT_COLOR(90, "{}") "  [" VT_COLOR(93,  "WARN") "] "              "{}"  "\n";
            break;case LogSemantic::error: format = VT_COLOR(90, "{}") " ["  VT_COLOR(91, "ERROR") "] "              "{}"  "\n";
            break;case LogSemantic::fatal: format = VT_COLOR(90, "{}") " ["  VT_COLOR(91, "FATAL") "] "              "{}"  "\n";
        }
    }

    auto time_ms = FmtTime{timestamp, TimeFormat::time_ms};
    std::cerr << std::vformat(format, std::make_format_args(time_ms, message)) << std::flush;

    if (state.log_file.is_open()) {
        auto[stacktrace, new_stacktrace] = state.stacktraces.insert(std::stacktrace::current(1));
        if (new_stacktrace) {
            state.log_file << std::format("s {}\n", (void*)stacktrace);
            for (auto& entry : *stacktrace) {
                if (entry.source_file().empty() && entry.description().empty()) continue;
                state.log_file << std::format("- {} \"{}\" {}\n", entry.source_line(), entry.source_file(), entry.description());
            }
        }

        state.log_file << std::format("m {} {} {}\n", (void*)stacktrace, FmtTime{timestamp, TimeFormat::iso8601}, semantic);
        for (auto line : std::views::split(message, '\n')) {
            state.log_file << std::format("- {:s}\n", line);
        }
        state.log_file.flush();
    }
}
