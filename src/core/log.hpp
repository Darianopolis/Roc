#pragma once

#include "pch.hpp"
#include "types.hpp"
#include "signal.hpp"

enum class LogSemantic : u32
{
    trace,
    debug,
    info,
    warn,
    error,
    fatal,
};

void log_set_structured_log(const std::filesystem::path&);
void log_redirect_stdout(   const std::filesystem::path&);
void log_redirect_stderr(   const std::filesystem::path&);

void log(LogSemantic, std::string_view message);

template<LogSemantic semantic, typename ...Args>
void log(std::format_string<Args...> fmt, Args&&... args)
{
    log(semantic, std::format(fmt, std::forward<Args>(args)...));
}

#define log_trace log<LogSemantic::trace>
#define log_debug log<LogSemantic::debug>
#define log_info  log<LogSemantic::info>
#define log_warn  log<LogSemantic::warn>
#define log_error log<LogSemantic::error>
