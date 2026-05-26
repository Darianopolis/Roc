#include "pch.hpp"
#include "log.hpp"
#include "stacktrace.hpp"
#include "chrono.hpp"
#include "enum.hpp"

#define VT_COLOR_BEGIN(color) "\u001B[" #color "m"
#define VT_COLOR_RESET "\u001B[0m"
#define VT_COLOR(color, text) VT_COLOR_BEGIN(color) text VT_COLOR_RESET

struct LogState {
    std::ofstream log_file;
    bool is_stderr_redirected;

    StacktraceCache stacktraces;
    std::recursive_mutex mutex;
};

static LogState* log_state;

void log_init(const LogInitInfo& info)
{
    log_state = new LogState {};

    if (info.log_path) {
        log_state->log_file = std::ofstream(*info.log_path);
    }

    if (info.stdout_redirect) {
        unix_check<freopen>(info.stdout_redirect->c_str(), "w", stdout);
    }

    if (info.stderr_redirect) {
        log_state->is_stderr_redirected = true;
        unix_check<freopen>(info.stderr_redirect->c_str(), "w", stderr);
    }
}

void log_deinit()
{
    delete log_state;
}

void log(LogSemantic semantic, std::string_view message)
{
    auto& state = *log_state;

    // Strip trailing newlines
    while (message.ends_with('\n')) message.remove_suffix(1);

    auto timestamp = time_current();

    std::scoped_lock _ { state.mutex };

    auto[stacktrace, new_stacktrace] = state.stacktraces.insert(std::stacktrace::current(1));

    if (state.log_file.is_open()) {
        if (new_stacktrace) {
            state.log_file << std::format("s {}\n", (void*)stacktrace);
            for (auto& entry : *stacktrace) {
                if (entry.source_file().empty() && entry.description().empty()) continue;
                state.log_file << std::format("- {} \"{}\" {}\n", entry.source_line(), entry.source_file(), entry.description());
                state.log_file.flush();
            }
        }
    }

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
        state.log_file << std::format("m {} {} {}\n", (void*)stacktrace, FmtTime{timestamp, TimeFormat::iso8601}, semantic);
        for (auto line : std::views::split(message, '\n')) {
            state.log_file << std::format("- {:s}\n", line);
        }
        state.log_file.flush();
    }
}
