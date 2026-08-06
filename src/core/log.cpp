#include "pch.hpp"
#include "log.hpp"
#include "chrono.hpp"
#include "enum.hpp"
#include "fd.hpp"
#include "process.hpp"

static Logger* logger;

Logger::Logger()
{
    debug_assert(!logger);
    logger = this;
}

Logger::~Logger()
{
    debug_assert(logger == this);
    logger = nullptr;
}

void log_set_structured_log(const std::filesystem::path& path)
{
    std::scoped_lock _ { logger->mutex };

    auto fd = path_open(path, O_RDWR | O_TRUNC | O_APPEND | O_CREAT, 0666);
    logger->log_file.reset(unix_check<fdopen>(fd.get(), "w").value);
    if (logger->log_file) fd.extract();
}

void log_redirect_stdout(const std::filesystem::path& path)
{
    unix_check<freopen>(path.c_str(), "w", stdout);
}

void log_redirect_stderr(const std::filesystem::path& path)
{
    std::scoped_lock _ { logger->mutex };

    logger->is_stderr_redirected = true;
    unix_check<freopen>(path.c_str(), "w", stderr);
}

void log(LogSemantic semantic, std::string_view message)
{
    // Strip trailing newlines
    while (message.ends_with('\n')) message.remove_suffix(1);

    auto timestamp = std::chrono::system_clock::now();

    std::scoped_lock _ { logger->mutex };

#define LOG(...) std::println(stderr, __VA_ARGS__, FmtTime{timestamp, TimeFormat::time_ms}, message)
#define LOG_COLOR(color, text) "\u001B[" #color "m" text "\u001B[0m"

    if (logger->is_stderr_redirected) {
        switch (semantic) {
            break;case LogSemantic::trace: LOG("{} [TRACE] {}");
            break;case LogSemantic::debug: LOG("{} [DEBUG] {}");
            break;case LogSemantic::info:  LOG("{}  [INFO] {}");
            break;case LogSemantic::warn:  LOG("{}  [WARN] {}");
            break;case LogSemantic::error: LOG("{} [ERROR] {}");
            break;case LogSemantic::fatal: LOG("{} [FATAL] {}");
        }
    } else {
        switch (semantic) {
            break;case LogSemantic::trace: LOG(LOG_COLOR(90, "{}") " ["  LOG_COLOR(90, "TRACE") "] " LOG_COLOR(90, "{}"));
            break;case LogSemantic::debug: LOG(LOG_COLOR(90, "{}") " ["  LOG_COLOR(96, "DEBUG") "] "               "{}" );
            break;case LogSemantic::info:  LOG(LOG_COLOR(90, "{}") "  [" LOG_COLOR(94,  "INFO") "] "               "{}" );
            break;case LogSemantic::warn:  LOG(LOG_COLOR(90, "{}") "  [" LOG_COLOR(93,  "WARN") "] "               "{}" );
            break;case LogSemantic::error: LOG(LOG_COLOR(90, "{}") " ["  LOG_COLOR(91, "ERROR") "] "               "{}" );
            break;case LogSemantic::fatal: LOG(LOG_COLOR(90, "{}") " ["  LOG_COLOR(91, "FATAL") "] "               "{}" );
        }
    }
    fflush(stderr);

    if (auto* out = logger->log_file.get()) {
        auto[stacktrace, new_stacktrace] = logger->stacktraces.insert(std::stacktrace::current(1));
        if (new_stacktrace) {
            std::println(out, "s {}", (void*)stacktrace);
            for (auto& entry : *stacktrace) {
                if (entry.source_file().empty() && entry.description().empty()) continue;
                std::println(out, "- {} \"{}\" {}", entry.source_line(), entry.source_file(), entry.description());
            }
        }

        std::println(out, "m {} {} {}", (void*)stacktrace, FmtTime{timestamp, TimeFormat::iso8601}, semantic);
        for (auto line : std::views::split(message, '\n')) {
            std::println(out, "- {:s}", line);
        }
        fflush(out);
    }
}
