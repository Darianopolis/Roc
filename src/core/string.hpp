#pragma once

#include "types.hpp"

// -----------------------------------------------------------------------------

namespace core
{
    constexpr
    u32 decimals_for_3sf(f64 value)
    {
        if (value < 10) return 2;
        if (value < 100) return 1;
        return 0;
    }

    auto format_with_suffix(std::string_view suffix, f64 amount, u32 decimals) -> std::string;
}

// -----------------------------------------------------------------------------

namespace core
{
    constexpr
    std::string ascii_to_upper(std::string_view in)
    {
        std::string out(in);
        for (char& c : out) c = std::toupper(c);
        return out;
    }
}

// -----------------------------------------------------------------------------

namespace core
{
    inline
    std::string replace_suffix(std::string_view in, std::string_view old_suffix, std::string_view new_suffix)
    {
        return std::format("{}{}", in.substr(0, in.size() - old_suffix.size()), new_suffix);
    }

    inline
    std::string escape_utf8(std::string_view in)
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
}
