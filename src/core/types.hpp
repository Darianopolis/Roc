#pragma once

#include "pch.hpp"

static_assert(std::endian::native == std::endian::little);

// -----------------------------------------------------------------------------

using u64 = uint64_t;
using u32 = uint32_t;
using u16 = uint16_t;
using u8 = uint8_t;

using i64 = int64_t;
using i32 = int32_t;
using i16 = int16_t;
using i8 = int8_t;

using usz = size_t;
using isz = i64;

using c32 = char32_t;

using byte = std::byte;

using f32 = float;
using f64 = double;

// -----------------------------------------------------------------------------

namespace core
{
    template<glm::length_t L, typename T>
    using Vec = glm::vec<L, T>;
}

using vec2u32 = core::Vec<2, u32>;
using vec2i32 = core::Vec<2, i32>;
using vec2f32 = core::Vec<2, f32>;
using vec2f64 = core::Vec<2, f64>;

using vec3f32 = core::Vec<3, f32>;

using vec4f32 = core::Vec<4, f32>;
using vec4u8  = core::Vec<4,  u8>;

// -----------------------------------------------------------------------------

namespace core
{
    template<typename T>
    struct Aabb;

    namespace detail {
        struct xywh_tag   {};
        struct minmax_tag {};
    }

    static constexpr detail::xywh_tag   xywh;
    static constexpr detail::minmax_tag minmax;

    template<typename T>
    struct Rect
    {
        core::Vec<2, T> origin, extent;

        constexpr Rect() = default;

        constexpr Rect(core::Vec<2, T> origin, core::Vec<2, T> extent, detail::xywh_tag)
            : origin(origin)
            , extent(extent)
        {}

        constexpr Rect(core::Vec<2, T> min, core::Vec<2, T> max, detail::minmax_tag)
            : origin(min)
            , extent(max - min)
        {}

        template<typename T2>
            requires (!std::same_as<T2, T>)
        constexpr Rect(const core::Rect<T2>& other)
            : Rect(other.origin, other.extent, xywh)
        {}

        template<typename T2>
        constexpr Rect(const core::Aabb<T2>& other)
            : Rect(other.min, other.max, minmax)
        {}

        constexpr bool operator==(const core::Rect<T>& other) const = default;
    };
}

using rect2i32 = core::Rect<i32>;
using rect2f32 = core::Rect<f32>;
using rect2f64 = core::Rect<f64>;

// -----------------------------------------------------------------------------

template<typename T>
struct core::Aabb
{
    core::Vec<2, T> min, max;

    constexpr Aabb() = default;

    constexpr Aabb(core::Vec<2, T> origin, core::Vec<2, T> extent, detail::xywh_tag)
        : min(origin)
        , max(origin + extent)
    {}

    constexpr Aabb(core::Vec<2, T> min, core::Vec<2, T> max, detail::minmax_tag)
        : min(min)
        , max(max)
    {}

    template<typename T2>
        requires (!std::same_as<T2, T>)
    constexpr Aabb(const core::Aabb<T2>& other)
        : Aabb(other.min, other.max, minmax)
    {}

    template<typename T2>
    constexpr Aabb(const core::Rect<T2>& other)
        : Aabb(other.origin, other.extent, xywh)
    {}

    constexpr bool operator==(const core::Aabb<T>& other) const = default;
};

using aabb2i32 = core::Aabb<i32>;
using aabb2f32 = core::Aabb<f32>;
using aabb2f64 = core::Aabb<f64>;
