#include "process.hpp"

#include "util.hpp"
#include "debug.hpp"

bool core::capability::has(cap_value_t cap)
{
    cap_t caps = core::check<cap_get_proc>().value;
    if (!caps) return false;
    defer { core::check<cap_free>(caps); };
    cap_flag_value_t value = CAP_CLEAR;
    core::check<cap_get_flag>(caps, cap, CAP_EFFECTIVE, &value);
    return value == CAP_SET;
}

void core::capability::drop(cap_value_t cap)
{
    cap_t caps = core::check<cap_get_proc>().value;
    if (!caps) return;
    defer { core::check<cap_free>(caps); };
    core::check<cap_set_flag>(caps, CAP_EFFECTIVE, 1, &cap, CAP_CLEAR);
    core::check<cap_set_flag>(caps, CAP_PERMITTED, 1, &cap, CAP_CLEAR);
    core::check<cap_set_proc>(caps);
}
