#pragma once

inline
auto env_get(const char* name) -> std::optional<std::string>
{
    auto value = getenv(name);
    if (!value) return std::nullopt;
    return value;
}

inline
void env_set(const char* name, std::optional<std::string> value)
{
    if (value) {
        setenv(name, value->c_str(), true);
    } else {
        unsetenv(name);
    }
}
