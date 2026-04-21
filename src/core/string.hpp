#pragma once

#include "types.hpp"

// -----------------------------------------------------------------------------

constexpr
auto ascii_to_upper(std::string_view in) -> std::string
{
    std::string out(in);
    for (char& c : out) c = std::toupper(c);
    return out;
}

// -----------------------------------------------------------------------------

inline
auto replace_suffix(std::string_view in, std::string_view old_suffix, std::string_view new_suffix) -> std::string
{
    return std::format("{}{}", in.substr(0, in.size() - old_suffix.size()), new_suffix);
}

inline
auto escape_utf8(std::string_view in) -> std::string
{
    std::string out;
    for (char c : in) {
        switch (c) {
            break;case '\r': out += "\\r";
            break;case '\n': out += "\\n";
            break;case '\b': out += "\\b";
            break;case '\t': out += "\\t";
            break;case '\f': out += "\\f";
            break;default:
                if (::isalpha(c) || ::isdigit(c)) {
                    out += c;
                } else {
                    out += std::format("\\{:x}", c);
                }
        }
    }
    return out;
}
