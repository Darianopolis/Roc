#pragma once

#include "types.hpp"
#include "debug.hpp"

constexpr
auto color_from_hex(std::string_view str) -> vec4u8
{
    vec4u8 color;
    if (str.starts_with("#")) str.remove_prefix(1);

    static constexpr
    auto hex_to_value = [](char digit) -> u8 {
        switch (digit) {
            break;case 'a' ... 'f': return 10 + digit - 'a';
            break;case 'A' ... 'F': return 10 + digit - 'A';
            break;case '0' ... '9': return digit - '0';
            break;default:
                debug_unreachable();
        }
    };

    auto hex_pair_to_value = [&](u32 i) -> u8 {
        return hex_to_value(str[i]) * 16 + hex_to_value(str[i + 1]);
    };

    debug_assert(str.size() >= 6);

    color.x = hex_pair_to_value(0);
    color.y = hex_pair_to_value(2);
    color.z = hex_pair_to_value(4);
    color.w = str.size() >= 8 ? hex_pair_to_value(6) : 255;

    return color;
}

constexpr
auto color_hsv_to_rgb(vec4f32 hsv) -> vec4f32
{
    f32 h = hsv.x * 360.f;
    f32 s = std::clamp(hsv.y, 0.f, 1.f);
    f32 v = std::clamp(hsv.z, 0.f, 1.f);

    h = std::fmod(h, 360.f);
    if (h < 0.f) h += 360.f;

    f32 c  = v * s;
    f32 hp = h / 60.f;
    f32 x  = c * (1.f - std::fabs(std::fmod(hp, 2.f) - 1.f));
    f32 m  = v - c;

    f32 r, g, b;
    if      (hp < 1.f) { r = c;   g = x;   b = 0.f; }
    else if (hp < 2.f) { r = x;   g = c;   b = 0.f; }
    else if (hp < 3.f) { r = 0.f; g = c;   b = x;   }
    else if (hp < 4.f) { r = 0.f; g = x;   b = c;   }
    else if (hp < 5.f) { r = x;   g = 0.f; b = c;   }
    else               { r = c;   g = 0.f; b = x;   }

    return { r + m, g + m, b + m, hsv.w };
}
