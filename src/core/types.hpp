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

template<u8 L, typename T>
struct Vec;

#define VEC_CONSTRUCTORS 0

template<typename T>
struct Vec<2, T>
{
    T x, y;

#if VEC_CONSTRUCTORS
    constexpr Vec() = default;
    constexpr Vec(T x, T y): x(x), y(y) {}
#endif

    constexpr auto operator<=>(const Vec&) const -> std::strong_ordering = default;

    constexpr auto operator[](usz i) const -> T  { return i == 1 ? y : x; }
    constexpr auto operator[](usz i)       -> T& { return i == 1 ? y : x; }

    constexpr decltype(auto) operator+=(T s) { x += s; y += s; return *this; }
    constexpr decltype(auto) operator-=(T s) { x -= s; y -= s; return *this; }
    constexpr decltype(auto) operator*=(T s) { x *= s; y *= s; return *this; }
    constexpr decltype(auto) operator/=(T s) { x /= s; y /= s; return *this; }

    constexpr decltype(auto) operator+=(Vec b) { x += b.x; y += b.y; return *this; }
    constexpr decltype(auto) operator-=(Vec b) { x -= b.x; y -= b.y; return *this; }
    constexpr decltype(auto) operator*=(Vec b) { x *= b.x; y *= b.y; return *this; }
    constexpr decltype(auto) operator/=(Vec b) { x /= b.x; y /= b.y; return *this; }
};

template<typename T>
struct Vec<4, T>
{
    T x, y, z, w;

#if VEC_CONSTRUCTORS
    constexpr Vec() = default;
    constexpr Vec(T x, T y, T z, T w): x(x), y(y), z(z), w(w) {}
#endif

    constexpr auto operator<=>(const Vec&) const -> std::strong_ordering = default;
};

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

    constexpr Rect(const Aabb<T>& other)
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

    constexpr Aabb(const Rect<T>& other)
        : Aabb(other.origin, other.extent, xywh)
    {}

    constexpr auto operator==(const Aabb<T>& other) const -> bool = default;
};

using aabb2i32 = Aabb<i32>;
using aabb2f32 = Aabb<f32>;
using aabb2f64 = Aabb<f64>;
