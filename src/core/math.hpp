#pragma once

#include "types.hpp"

// -----------------------------------------------------------------------------

template<typename T> constexpr auto operator-(Vec<2, T> v) -> Vec<2, T> const { return {-v.x, -v.y}; }

// -----------------------------------------------------------------------------

template<typename T> constexpr auto operator+(Vec<2, T> a, Vec<2, T> b) -> Vec<2, T> { return {a.x + b.x, a.y + b.y}; }
template<typename T> constexpr auto operator-(Vec<2, T> a, Vec<2, T> b) -> Vec<2, T> { return {a.x - b.x, a.y - b.y}; }
template<typename T> constexpr auto operator*(Vec<2, T> a, Vec<2, T> b) -> Vec<2, T> { return {a.x * b.x, a.y * b.y}; }
template<typename T> constexpr auto operator/(Vec<2, T> a, Vec<2, T> b) -> Vec<2, T> { return {a.x / b.x, a.y / b.y}; }

// -----------------------------------------------------------------------------

template<typename T> constexpr auto operator+(Vec<2, T> a, T b) -> Vec<2, T> { return {a.x + b, a.y + b}; }
template<typename T> constexpr auto operator-(Vec<2, T> a, T b) -> Vec<2, T> { return {a.x - b, a.y - b}; }
template<typename T> constexpr auto operator*(Vec<2, T> a, T b) -> Vec<2, T> { return {a.x * b, a.y * b}; }
template<typename T> constexpr auto operator/(Vec<2, T> a, T b) -> Vec<2, T> { return {a.x / b, a.y / b}; }

template<typename T> constexpr auto operator/(Vec<4, T> a, T b) -> Vec<4, T> { return {a.x / b, a.y / b, a.z / b, a.w / b}; }

// -----------------------------------------------------------------------------

template<typename T> constexpr auto operator+(T a, Vec<2, T> b) -> Vec<2, T> { return {a + b.x, a + b.y}; }
template<typename T> constexpr auto operator-(T a, Vec<2, T> b) -> Vec<2, T> { return {a - b.x, a - b.y}; }
template<typename T> constexpr auto operator*(T a, Vec<2, T> b) -> Vec<2, T> { return {a * b.x, a * b.y}; }
template<typename T> constexpr auto operator/(T a, Vec<2, T> b) -> Vec<2, T> { return {a / b.x, a / b.y}; }

// -----------------------------------------------------------------------------

template<typename To, typename From> constexpr auto vec_cast(Vec<2, From> v) -> Vec<2, To> { return { To(v.x), To(v.y) }; }

template<typename To, typename From> constexpr auto vec_cast(Vec<4, From> v) -> Vec<4, To> { return { To(v.x), To(v.y), To(v.z), To(v.w) }; }

// -----------------------------------------------------------------------------

template<typename T> constexpr auto vec_abs(  Vec<2, T> v) -> Vec<2, T> { return { std::abs(  v.x), std::abs(  v.y) }; }
template<typename T> constexpr auto vec_floor(Vec<2, T> v) -> Vec<2, T> { return { std::floor(v.x), std::floor(v.y) }; }
template<typename T> constexpr auto vec_round(Vec<2, T> v) -> Vec<2, T> { return { std::round(v.x), std::round(v.y) }; }

// -----------------------------------------------------------------------------

template<typename T> constexpr auto vec_min(Vec<2, T> a, Vec<2, T> b) -> Vec<2, T> { return { std::min(a.x, b.x), std::min(a.y, b.y) }; }
template<typename T> constexpr auto vec_max(Vec<2, T> a, Vec<2, T> b) -> Vec<2, T> { return { std::max(a.x, b.x), std::max(a.y, b.y) }; }

template<typename T>
constexpr
auto vec_clamp(Vec<2, T> value, Vec<2, T> min, Vec<2, T> max) -> Vec<2, T>
{
    return vec_max(vec_min(value, max), min);
}

// -----------------------------------------------------------------------------

template<typename T>
constexpr
auto vec_copysign(Vec<2, T> v, Vec<2, T> s) -> Vec<2, T>
{
    return {std::copysign(v.x, s.x), std::copysign(v.y, s.y)};
}

template<typename T>
constexpr
auto vec_round_to_zero(Vec<2, T> v) -> Vec<2, T> {
    return vec_copysign(vec_floor(vec_abs(v)), v);
}

// -----------------------------------------------------------------------------

template<typename T>
constexpr
auto vec_distance(Vec<2, T> a, Vec<2, T> b) -> T
{
    auto d = a - b;
    return std::sqrt(d.x * d.x + d.y * d.y);
}

// -----------------------------------------------------------------------------

template<typename T>
struct std::formatter<Vec<2, T>> {
    constexpr auto parse(auto& ctx) { return ctx.begin(); }
    constexpr auto format(const Vec<2, T>& v, auto& ctx) const
    {
        return std::format_to(ctx.out(), "({}, {})", v.x, v.y);
    }
};

template<typename T>
struct std::formatter<Vec<3, T>> {
    constexpr auto parse(auto& ctx) { return ctx.begin(); }
    constexpr auto format(const Vec<3, T>& v, auto& ctx) const
    {
        return std::format_to(ctx.out(), "({}, {}, {})", v.x, v.y, v.z);
    }
};

template<typename T>
struct std::formatter<Vec<4, T>> {
    constexpr auto parse(auto& ctx) { return ctx.begin(); }
    constexpr auto format(const Vec<4, T>& v, auto& ctx) const
    {
        return std::format_to(ctx.out(), "({}, {}, {}, {})", v.x, v.y, v.z, v.w);
    }
};

template<typename T>
struct std::formatter<Rect<T>> {
    constexpr auto parse(auto& ctx) { return ctx.begin(); }
    constexpr auto format(const Rect<T>& r, auto& ctx) const
    {
        return std::format_to(ctx.out(), "(({}, {}) : ({}, {}))", r.origin.x, r.origin.y, r.extent.x, r.extent.y);
    }
};

template<typename T>
struct std::formatter<Aabb<T>> {
    constexpr auto parse(auto& ctx) { return ctx.begin(); }
    constexpr auto format(const Aabb<T>& a, auto& ctx) const
    {
        return std::format_to(ctx.out(), "(({}, {}) < ({}, {}))", a.min.x, a.min.y, a.max.x, a.max.y);
    }
};

// -----------------------------------------------------------------------------

template<typename To, typename From>
constexpr
auto aabb_cast(const Rect<From>& from) -> Aabb<To>
{
    return {
        vec_cast<To>(from.origin),
        vec_cast<To>(from.extent),
        xywh,
    };
}

template<typename To, typename From>
constexpr
auto aabb_cast(const Aabb<From>& from) -> Aabb<To>
{
    return {
        vec_cast<To>(from.min),
        vec_cast<To>(from.max),
        minmax,
    };
}

template<typename T>
constexpr
auto aabb_clamp_point(const Aabb<T>& rect, Vec<2, T> point) -> Vec<2, T>
{
    return vec_clamp(point, rect.min, rect.max);
}

template<typename T>
constexpr
auto aabb_contains(const Aabb<T>& rect, Vec<2, T> point) -> bool
{
    return point.x >= rect.min.x && point.x < rect.max.x
        && point.y >= rect.min.y && point.y < rect.max.y;
}

template<typename T>
constexpr
auto aabb_outer(const Aabb<T>& a, const Aabb<T>& b) -> Aabb<T>
{
    return {vec_min(a.min, b.min), vec_max(a.max, b.max), minmax};
}

template<typename T>
constexpr
auto aabb_inner(const Aabb<T>& a, const Aabb<T>& b) -> Aabb<T>
{
    return {vec_max(a.min, b.min), vec_min(a.max, b.max), minmax};
}

template<typename T>
constexpr
auto aabb_intersects(const Aabb<T>& a, const Aabb<T>& b, Aabb<T>* intersection = nullptr) -> bool
{
    auto i = aabb_inner(a, b);

    if (i.max.x <= i.min.x || i.max.y <= i.min.y) {
        if (intersection) *intersection = {};
        return false;
    } else {
        if (intersection) *intersection = i;
        return true;
    }
}

template<typename T>
constexpr
auto aabb_constrain(Aabb<T> aabb, const Aabb<T>& bounds) -> Rect<T>
{
    auto constrain_axis = [&](usz axis) {
        if (aabb.max[axis] - aabb.min[axis] > bounds.max[axis] - bounds.min[axis]) {
            aabb.min[axis] = bounds.min[axis];
            aabb.max[axis] = bounds.max[axis];
        } else if (aabb.min[axis] < bounds.min[axis]) {
            aabb.max[axis] += (bounds.min[axis] - aabb.min[axis]);
            aabb.min[axis] = bounds.min[axis];
        } else if (aabb.max[axis] > bounds.max[axis]) {
            aabb.min[axis] -= (aabb.max[axis] - bounds.max[axis]);
            aabb.max[axis] = bounds.max[axis];
        }
    };
    constrain_axis(0);
    constrain_axis(1);
    return aabb;
}

template<typename T>
constexpr
auto aabb_subtract(const Aabb<T>& minuend, const Aabb<T>& subtrahend, Aabb<T>* out) -> u32
{
    Aabb<T> intersection;
    if (aabb_intersects(minuend, subtrahend, &intersection)) {
        u32 count = 0;
        if (minuend.min.x != intersection.min.x) /* left   */ out[count++] = {{     minuend.min.x, intersection.min.y}, {intersection.min.x, intersection.max.y}, minmax};
        if (minuend.max.x != intersection.max.x) /* right  */ out[count++] = {{intersection.max.x, intersection.min.y}, {     minuend.max.x, intersection.max.y}, minmax};
        if (minuend.min.y != intersection.min.y) /* top    */ out[count++] = {{     minuend.min},                       {     minuend.max.x, intersection.min.y}, minmax};
        if (minuend.max.y != intersection.max.y) /* bottom */ out[count++] = {{     minuend.min.x, intersection.max.y}, {     minuend.max                      }, minmax};
        return count;
    } else {
        *out = minuend;
        return 1;
    }
}

// -----------------------------------------------------------------------------

template<typename To, typename From>
constexpr
auto rect_cast(const Rect<From>& from) -> Rect<To>
{
    return {
        vec_cast<To>(from.origin),
        vec_cast<To>(from.extent),
        xywh,
    };
}

template<typename To, typename From>
constexpr
auto rect_cast(const Aabb<From>& from) -> Rect<To>
{
    return {
        vec_cast<To>(from.min),
        vec_cast<To>(from.max),
        minmax,
    };
}

template<typename T>
constexpr
auto rect_clamp_point(const Rect<T>& rect, Vec<2, T> point) -> Vec<2, T>
{
    return aabb_clamp_point<T>(rect, point);
}

template<typename T>
constexpr
auto rect_contains(const Rect<T>& rect, Vec<2, T> point) -> bool
{
    return aabb_contains<T>(rect, point);
}

template<typename T>
constexpr
auto rect_intersects(const Rect<T>& a, const Rect<T>& b, Rect<T>* intersection = nullptr) -> bool
{
    Aabb<T> i;
    bool intersects = aabb_intersects<T>(a, b, &i);
    if (intersection) *intersection = i;
    return intersects;
}

// -----------------------------------------------------------------------------

template<typename T>
constexpr
auto rect_fit(Vec<2, T> outer, Vec<2, T> inner) -> Rect<T>
{
    T scale = std::min(outer.x / inner.x, outer.y / inner.y);
    auto extent = inner * scale;
    auto offset = (outer - extent) / T(2);
    return {offset, extent, xywh};
}

// -----------------------------------------------------------------------------

template<typename Out, typename In>
constexpr
auto pixel_round(Vec<2, In> pos, Vec<2, In>* remainder = nullptr) -> Vec<2, Out>
{
    // For points, we floor to treat the position as any point within a given integer region
    auto rounded = vec_floor(pos);
    if (remainder) *remainder = pos - rounded;
    return rounded;
}

template<typename Out, typename In>
constexpr
auto pixel_round(Rect<In> rect, Rect<In>* remainder = nullptr) -> Rect<Out>
{
    Aabb<In> bounds = rect;
    auto min = bounds.min;
    auto max = bounds.max;
    // For rects, we round as the min and max are treated as integer boundaries
    auto extent = vec_round(max - min);
    auto origin = vec_round(min);
    if (remainder) {
        *remainder = {
            min - origin,
            max - min - (extent),
            xywh,
        };
    }
    return { origin, extent, xywh };
}
