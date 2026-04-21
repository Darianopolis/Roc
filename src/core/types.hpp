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

template<glm::length_t L, typename T>
using Vec = glm::vec<L, T>;

using vec2u32 = Vec<2, u32>;
using vec2i32 = Vec<2, i32>;
using vec2f32 = Vec<2, f32>;
using vec2f64 = Vec<2, f64>;

using vec3f32 = Vec<3, f32>;

using vec4f32 = Vec<4, f32>;
using vec4u8  = Vec<4,  u8>;

// -----------------------------------------------------------------------------

template<typename T>
struct Aabb;

// -----------------------------------------------------------------------------

namespace detail {
    struct XywhTag   {};
    struct MinmaxTag {};
}

static constexpr detail::XywhTag   xywh;
static constexpr detail::MinmaxTag minmax;

template<typename T>
struct Rect
{
    Vec<2, T> origin, extent;

    constexpr Rect() = default;

    constexpr Rect(Vec<2, T> origin, Vec<2, T> extent, detail::XywhTag)
        : origin(origin)
        , extent(extent)
    {}

    constexpr Rect(Vec<2, T> min, Vec<2, T> max, detail::MinmaxTag)
        : origin(min)
        , extent(max - min)
    {}

    template<typename T2>
        requires (!std::same_as<T2, T>)
    constexpr Rect(const Rect<T2>& other)
        : Rect(other.origin, other.extent, xywh)
    {}

    template<typename T2>
    constexpr Rect(const Aabb<T2>& other)
        : Rect(other.min, other.max, minmax)
    {}

    constexpr auto operator==(const Rect<T>& other) const -> bool = default;
};

using rect2i32 = Rect<i32>;
using rect2f32 = Rect<f32>;
using rect2f64 = Rect<f64>;

// -----------------------------------------------------------------------------

template<typename T>
struct Aabb
{
    Vec<2, T> min, max;

    constexpr Aabb() = default;

    constexpr Aabb(Vec<2, T> origin, Vec<2, T> extent, detail::XywhTag)
        : min(origin)
        , max(origin + extent)
    {}

    constexpr Aabb(Vec<2, T> min, Vec<2, T> max, detail::MinmaxTag)
        : min(min)
        , max(max)
    {}

    template<typename T2>
        requires (!std::same_as<T2, T>)
    constexpr Aabb(const Aabb<T2>& other)
        : Aabb(other.min, other.max, minmax)
    {}

    template<typename T2>
    constexpr Aabb(const Rect<T2>& other)
        : Aabb(other.origin, other.extent, xywh)
    {}

    constexpr auto operator==(const Aabb<T>& other) const -> bool = default;
};

using aabb2i32 = Aabb<i32>;
using aabb2f32 = Aabb<f32>;
using aabb2f64 = Aabb<f64>;
