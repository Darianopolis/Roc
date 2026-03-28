#include "pch.hpp"
#include "log.hpp"
#include "stacktrace.hpp"
#include "chrono.hpp"

#define VT_COLOR_BEGIN(color) "\u001B[" #color "m"
#define VT_COLOR_RESET "\u001B[0m"
#define VT_COLOR(color, text) VT_COLOR_BEGIN(color) text VT_COLOR_RESET

static struct {
    LogLevel log_level = LogLevel::trace;
    std::ofstream log_file;

    struct {
        std::string buffer;
        std::vector<LogEntry> entries;
        u32 lines;
        bool enabled;
    } history;

    StacktraceCache stacktraces;
    std::recursive_mutex mutex;
} log_state = {};

LogLevel log_get_level()
{
    return log_state.log_level;
}

bool log_is_enabled(LogLevel level)
{
    return level >= log_get_level();
}

bool log_is_history_enabled()
{
    std::scoped_lock _ { log_state.mutex };

    return log_state.history.enabled;
}

void log_set_history_enabled(bool enabled)
{
    std::scoped_lock _ { log_state.mutex };

    log_state.history.enabled = enabled;
}

void log_clear_history()
{
    std::scoped_lock _ { log_state.mutex };

    log_state.history.buffer.clear();
    log_state.history.entries.clear();
    log_state.history.lines = 0;
}

LogHistory log_get_history()
{
    std::unique_lock lock { log_state.mutex };
    return {
        std::move(lock),
        log_state.history.entries,
        log_state.history.lines,
        log_state.history.buffer.size()
    };
}

std::string_view LogEntry::message() const noexcept
{
    return std::string_view(log_state.history.buffer).substr(start, len);
}

const LogEntry* LogHistory::find(u32 line) const noexcept
{
    auto& state = log_state;

    struct Compare
    {
        bool operator()(const LogEntry& entry, u32 line)
        {
            return entry.line_start < line;
        }

        bool operator()(u32 line, const LogEntry& entry)
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

void log(LogLevel level, std::string_view message)
{
    auto& state = log_state;

    if (state.log_level > level) return;

    // Strip trailing newlines
    while (message.ends_with('\n')) message.remove_suffix(1);

    auto timestamp = time_current();

    std::scoped_lock _ { state.mutex };

    if (state.history.enabled) {
        auto[stacktrace, added] = state.stacktraces.insert(std::stacktrace::current(1));

        auto start = state.history.buffer.size();
        state.history.buffer.append(message);
        auto lines = u32(std::ranges::count(message, '\n') + 1);
        state.history.entries.emplace_back(LogEntry {
            .level = level,
            .timestamp = timestamp,
            .start = u32(start),
            .len = u32(message.size()),
            .line_start = state.history.lines,
            .lines = lines,
            .stacktrace = stacktrace,
        });
        state.history.lines += lines;
    }

    struct {
        const char* vt;
        const char* plain;
    } fmt;

    switch (level) {
        break;case LogLevel::trace:
            fmt = { VT_COLOR(90, "{}") " [" VT_COLOR(90, "TRACE") "] " VT_COLOR(90, "{}") "\n", "{} [TRACE] {}\n" };
        break;case LogLevel::debug:
            fmt = { VT_COLOR(90, "{}") " [" VT_COLOR(96, "DEBUG") "] {}\n",                          "{} [DEBUG] {}\n" };
        break;case LogLevel::info:
            fmt = { VT_COLOR(90, "{}") "  [" VT_COLOR(94, "INFO") "] {}\n",                          "{}  [INFO] {}\n" };
        break;case LogLevel::warn:
            fmt = { VT_COLOR(90, "{}") "  [" VT_COLOR(93, "WARN") "] {}\n",                          "{}  [WARN] {}\n" };
        break;case LogLevel::error:
            fmt = { VT_COLOR(90, "{}") " [" VT_COLOR(91, "ERROR") "] {}\n",                          "{} [ERROR] {}\n" };
        break;case LogLevel::fatal:
            fmt = { VT_COLOR(90, "{}") " [" VT_COLOR(91, "FATAL") "] {}\n",                          "{} [FATAL] {}\n" };
    }

    auto time_ms = FmtTime{timestamp, TimeFormat::time_ms};
    std::cout << std::vformat(fmt.vt, std::make_format_args(time_ms, message));
    if (state.log_file.is_open()) {
        auto datetime_ms = FmtTime{timestamp, TimeFormat::datetime_ms};
        state.log_file << std::vformat(fmt.plain, std::make_format_args(datetime_ms, message)) << std::flush;
    }
}

void log_init(LogLevel log_level,  const char* log_file)
{
    log_state.log_level = log_level;
    if (log_file) log_state.log_file = std::ofstream(log_file);
}
