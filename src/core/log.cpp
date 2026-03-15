#include "pch.hpp"
#include "log.hpp"
#include "stacktrace.hpp"
#include "chrono.hpp"

#define CORE_VT_COLOR_BEGIN(color) "\u001B[" #color "m"
#define CORE_VT_COLOR_RESET "\u001B[0m"
#define CORE_VT_COLOR(color, text) CORE_VT_COLOR_BEGIN(color) text CORE_VT_COLOR_RESET

static struct {
    core::LogLevel log_level = core::LogLevel::trace;
    std::ofstream log_file;

    struct {
        std::string buffer;
        std::vector<core::LogEntry> entries;
        u32 lines;
        bool enabled;
    } history;

    core::StacktraceCache stacktraces;
    std::recursive_mutex mutex;
} log_state = {};

core::LogLevel core::get_log_level()
{
    return log_state.log_level;
}

bool core::is_log_level_enabled(core::LogLevel level)
{
    return level >= core::get_log_level();
}

bool core::log_is_history_enabled()
{
    std::scoped_lock _ { log_state.mutex };

    return log_state.history.enabled;
}

void core::log_set_history_enabled(bool enabled)
{
    std::scoped_lock _ { log_state.mutex };

    log_state.history.enabled = enabled;
}

void core::log_clear_history()
{
    std::scoped_lock _ { log_state.mutex };

    log_state.history.buffer.clear();
    log_state.history.entries.clear();
    log_state.history.lines = 0;
}

core::LogHistory core::log_get_history()
{
    std::unique_lock lock { log_state.mutex };
    return {
        std::move(lock),
        log_state.history.entries,
        log_state.history.lines,
        log_state.history.buffer.size()
    };
}

std::string_view core::LogEntry::message() const noexcept
{
    return std::string_view(log_state.history.buffer).substr(start, len);
}

auto core::LogHistory::find(u32 line) const noexcept -> const core::LogEntry*
{
    auto& state = log_state;

    struct Compare
    {
        bool operator()(const core::LogEntry& entry, u32 line)
        {
            return entry.line_start < line;
        }

        bool operator()(u32 line, const core::LogEntry& entry)
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

void core::log(core::LogLevel level, std::string_view message)
{
    auto& state = log_state;

    if (state.log_level > level) return;

    // Strip trailing newlines
    while (message.ends_with('\n')) message.remove_suffix(1);

    auto timestamp = core::current_time();

    std::scoped_lock _ { state.mutex };

    if (state.history.enabled) {
        auto[stacktrace, added] = state.stacktraces.insert(std::stacktrace::current(1));

        auto start = state.history.buffer.size();
        state.history.buffer.append(message);
        auto lines = u32(std::ranges::count(message, '\n') + 1);
        state.history.entries.emplace_back(core::LogEntry {
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
        break;case core::LogLevel::trace:
            fmt = { CORE_VT_COLOR(90, "{}") " [" CORE_VT_COLOR(90, "TRACE") "] " CORE_VT_COLOR(90, "{}") "\n", "{} [TRACE] {}\n" };
        break;case core::LogLevel::debug:
            fmt = { CORE_VT_COLOR(90, "{}") " [" CORE_VT_COLOR(96, "DEBUG") "] {}\n",                          "{} [DEBUG] {}\n" };
        break;case core::LogLevel::info:
            fmt = { CORE_VT_COLOR(90, "{}") "  [" CORE_VT_COLOR(94, "INFO") "] {}\n",                          "{}  [INFO] {}\n" };
        break;case core::LogLevel::warn:
            fmt = { CORE_VT_COLOR(90, "{}") "  [" CORE_VT_COLOR(93, "WARN") "] {}\n",                          "{}  [WARN] {}\n" };
        break;case core::LogLevel::error:
            fmt = { CORE_VT_COLOR(90, "{}") " [" CORE_VT_COLOR(91, "ERROR") "] {}\n",                          "{} [ERROR] {}\n" };
        break;case core::LogLevel::fatal:
            fmt = { CORE_VT_COLOR(90, "{}") " [" CORE_VT_COLOR(91, "FATAL") "] {}\n",                          "{} [FATAL] {}\n" };
    }

    auto time_str = core::to_string(timestamp, core::TimeFormat::time_ms);
    std::cout << std::vformat(fmt.vt, std::make_format_args(time_str, message));
    if (state.log_file.is_open()) {
        auto datetime_str = core::to_string(timestamp, core::TimeFormat::datetime_ms);
        state.log_file << std::vformat(fmt.plain, std::make_format_args(datetime_str, message)) << std::flush;
    }
}

void core::init_log(core::LogLevel log_level,  const char* log_file)
{
    log_state.log_level = log_level;
    if (log_file) log_state.log_file = std::ofstream(log_file);
}
