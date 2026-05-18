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

    LogSignals signals;

    struct {
        std::string buffer;
        std::vector<LogEntry> entries;
        u32 lines;
        bool enabled;
    } history;

    StacktraceCache stacktraces;
    std::recursive_mutex mutex;
};

static LogState* log_state;

void log_init(const char* log_path)
{
    log_state = new LogState {};

    if (log_path) {
        log_state->log_file = std::ofstream(log_path);
    }
}

void log_deinit()
{
    delete log_state;
}

auto log_history_is_enabled() -> bool
{
    std::scoped_lock _ { log_state->mutex };

    return log_state->history.enabled;
}

void log_history_enable(bool enabled)
{
    std::scoped_lock _ { log_state->mutex };

    log_state->history.enabled = enabled;
}

auto log_history_get_signals() -> LogSignals&
{
    return log_state->signals;
}

void log_history_clear()
{
    std::scoped_lock _ { log_state->mutex };

    log_state->history.buffer.clear();
    log_state->history.entries.clear();
    log_state->history.lines = 0;
}

auto log_history_get() -> LogHistory
{
    std::unique_lock lock { log_state->mutex };
    return {
        std::move(lock),
        log_state->history.entries,
        log_state->history.lines,
        log_state->history.buffer.size()
    };
}

auto LogEntry::message() const noexcept -> std::string_view
{
    return std::string_view(log_state->history.buffer).substr(start, len);
}

auto LogHistory::find(u32 line) const noexcept -> const LogEntry*
{
    auto& state = *log_state;

    struct Compare
    {
        auto operator()(const LogEntry& entry, u32 line) -> bool
        {
            return entry.line_start < line;
        }

        auto operator()(u32 line, const LogEntry& entry) -> bool
        {
            return line < entry.line_start;
        }
    };

    if (state.history.entries.empty()) return nullptr;

    auto iter = std::upper_bound(state.history.entries.begin(), state.history.entries.end(), line, Compare{});

    // upper_bound will never return begin() in a non-empty list as:
    //  1) line >= 0
    //  2) the first entry will *always* have line_start == 0
    //  3) upper_bound will return the first entry *greater* than line
    // Hence, iter[-1] is safe here
    return &iter[-1];
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
            state.log_file << std::format("s  {}\n", (void*)stacktrace);
            for (auto& entry : *stacktrace) {
                if (entry.source_file().empty() && entry.description().empty()) continue;
                state.log_file << std::format("se {} \"{}\" {}\n", entry.source_line(), entry.source_file(), entry.description());
                state.log_file.flush();
            }
        }
    }

    if (state.history.enabled) {
        auto start = state.history.buffer.size();
        state.history.buffer.append(message);
        auto lines = u32(std::ranges::count(message, '\n') + 1);
        auto& entry = state.history.entries.emplace_back(LogEntry {
            .semantic = semantic,
            .timestamp = timestamp,
            .start = u32(start),
            .len = u32(message.size()),
            .line_start = state.history.lines,
            .lines = lines,
            .stacktrace = stacktrace,
        });
        state.history.lines += lines;

        state.signals.log_entry(&entry);
    }

    const char* format;
    switch (semantic) {
        break;case LogSemantic::trace: format = VT_COLOR(90, "{}") " ["  VT_COLOR(90, "TRACE") "] " VT_COLOR(90, "{}") "\n";
        break;case LogSemantic::debug: format = VT_COLOR(90, "{}") " ["  VT_COLOR(96, "DEBUG") "] "              "{}"  "\n";
        break;case LogSemantic::info:  format = VT_COLOR(90, "{}") "  [" VT_COLOR(94,  "INFO") "] "              "{}"  "\n";
        break;case LogSemantic::warn:  format = VT_COLOR(90, "{}") "  [" VT_COLOR(93,  "WARN") "] "              "{}"  "\n";
        break;case LogSemantic::error: format = VT_COLOR(90, "{}") " ["  VT_COLOR(91, "ERROR") "] "              "{}"  "\n";
        break;case LogSemantic::fatal: format = VT_COLOR(90, "{}") " ["  VT_COLOR(91, "FATAL") "] "              "{}"  "\n";
    }

    auto time_ms = FmtTime{timestamp, TimeFormat::time_ms};
    std::cerr << std::vformat(format, std::make_format_args(time_ms, message));

    if (state.log_file.is_open()) {
        state.log_file << std::format("m  {} {} {}\n", (void*)stacktrace, FmtTime{timestamp, TimeFormat::iso8601}, semantic);
        for (auto line : std::views::split(message, '\n')) {
            state.log_file << std::format("ml {:s}\n", line);
        }
        state.log_file.flush();
    }
}
