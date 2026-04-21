#pragma once

#include "types.hpp"

// -----------------------------------------------------------------------------

constexpr auto copysign(     vec2f64 v, vec2f64 s) -> vec2f64 { return vec2f64(std::copysign(v.x, s.x), std::copysign(v.y, s.y)); }
constexpr auto round_to_zero(vec2f64 v)            -> vec2f64 { return copysign(glm::floor(glm::abs(v)), v);                      }

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

template<typename T>
auto aabb_clamp_point(const Aabb<T>& rect, Vec<2, T> point) -> Vec<2, T>
{
    return glm::clamp(point, rect.min, rect.max);
}

template<typename T>
auto aabb_contains(const Aabb<T>& rect, Vec<2, T> point) -> bool
{
    return point.x >= rect.min.x && point.x < rect.max.x
        && point.y >= rect.min.y && point.y < rect.max.y;
}

template<typename T>
auto aabb_outer(const Aabb<T>& a, const Aabb<T>& b) -> Aabb<T>
{
    return {glm::min(a.min, b.min), glm::max(a.max, b.max), minmax};
}

template<typename T>
auto aabb_inner(const Aabb<T>& a, const Aabb<T>& b) -> Aabb<T>
{
    return {glm::max(a.min, b.min), glm::min(a.max, b.max), minmax};
}

template<typename T>
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
static
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

template<typename T>
auto rect_clamp_point(const Rect<T>& rect, Vec<2, T> point) -> Vec<2, T>
{
    return aabb_clamp_point<T>(rect, point);
}

template<typename T>
auto rect_contains(const Rect<T>& rect, Vec<2, T> point) -> bool
{
    return aabb_contains<T>(rect, point);
}

template<typename T>
auto rect_intersects(const Rect<T>& a, const Rect<T>& b, Rect<T>* intersection = nullptr) -> bool
{
    Aabb<T> i;
    bool intersects = aabb_intersects<T>(a, b, &i);
    if (intersection) *intersection = i;
    return intersects;
}

template<typename T>
auto rect_constrain(Rect<T> rect, const Rect<T>& bounds) -> Rect<T>
{
    static constexpr auto constrain_axis = [](T start, T length, T& origin, T& extent) {
        if (extent > length) {
            origin = start;
            extent = length;
        } else {
            origin = std::max(origin, start) - std::max(T(0), (origin + extent) - (start + length));
        }
    };
    constrain_axis(bounds.origin.x, bounds.extent.x, rect.origin.x, rect.extent.x);
    constrain_axis(bounds.origin.y, bounds.extent.y, rect.origin.y, rect.extent.y);
    return rect;
}

// -----------------------------------------------------------------------------

template<typename T>
auto rect_fit(Vec<2, T> outer, Vec<2, T> inner) -> Rect<T>
{
    T scale = glm::min(outer.x / inner.x, outer.y / inner.y);
    auto extent = inner * scale;
    auto offset = (outer - extent) / T(2);
    return {offset, extent, xywh};
}

// -----------------------------------------------------------------------------

template<typename Out, typename In>
auto round(Vec<2, In> pos, Vec<2, In>* remainder = nullptr) -> Vec<2, Out>
{
    // For points, we floor to treat the position as any point within a given integer region
    auto rounded = glm::floor(pos);
    if (remainder) *remainder = pos - rounded;
    return rounded;
}

template<typename Out, typename In>
auto round(Rect<In> rect, Rect<In>* remainder = nullptr) -> Rect<Out>
{
    Aabb<In> bounds = rect;
    auto min = bounds.min;
    auto max = bounds.max;
    // For rects, we round as the min and max are treated as integer boundaries
    auto extent = glm::round(max - min);
    auto origin = glm::round(min);
    if (remainder) {
        *remainder = {
            min - origin,
            max - min - (extent),
            xywh,
        };
    }
    return { origin, extent, xywh };
}
