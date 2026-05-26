#include "util.hpp"
#include "debug.hpp"

auto process_has_cap(cap_value_t cap) -> bool
{
    cap_t caps = unix_check<cap_get_proc>().value;
    if (!caps) return false;
    defer { unix_check<cap_free>(caps); };
    cap_flag_value_t value = CAP_CLEAR;
    unix_check<cap_get_flag>(caps, cap, CAP_EFFECTIVE, &value);
    return value == CAP_SET;
}

void process_drop_cap(cap_value_t cap)
{
    cap_t caps = unix_check<cap_get_proc>().value;
    if (!caps) return;
    defer { unix_check<cap_free>(caps); };
    unix_check<cap_set_flag>(caps, CAP_EFFECTIVE, 1, &cap, CAP_CLEAR);
    unix_check<cap_set_flag>(caps, CAP_PERMITTED, 1, &cap, CAP_CLEAR);
    unix_check<cap_set_proc>(caps);
}

void spawn_path(std::string_view view_name, std::span<const std::string_view> view_args)
{
    std::string name = std::string(view_name);

    std::vector<std::string> string_args;
    std::vector<char*> args;
    for (auto& varg : view_args) {
        string_args.emplace_back(varg);
    }
    for (auto& sarg : string_args) {
        args.emplace_back(sarg.data());
    }
    args.emplace_back(nullptr);

    if (fork() == 0) {
        // Unblock any signals currently blocked for signalfd handling purposes
        sigset_t mask;
        sigfillset(&mask);
        sigprocmask(SIG_UNBLOCK, &mask, nullptr);

        execvp(name.c_str(), args.data());
        std::terminate();
    }
}

auto env_get(const char* name) -> std::optional<std::string>
{
    auto value = getenv(name);
    if (!value) return std::nullopt;
    return value;
}

void env_set(const char* name, std::optional<std::string> value)
{
    if (value) {
        setenv(name, value->c_str(), true);
    } else {
        unsetenv(name);
    }
}
