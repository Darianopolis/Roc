#pragma once

#include "types.hpp"

// -----------------------------------------------------------------------------

#include "vec-ops.inl"

// -----------------------------------------------------------------------------

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
consteval
auto aabb_make_empty() -> Aabb<T>
{
    if constexpr (std::numeric_limits<T>::has_infinity) {
        auto inf = std::numeric_limits<T>::infinity();
        return {{inf, inf}, {-inf, -inf}, minmax};
    } else {
        auto min = std::numeric_limits<T>::lowest();
        auto max = std::numeric_limits<T>::max();
        return {{max, max}, {min, min}, minmax};
    }
}

template<typename T>
consteval
auto aabb_make_infinite() -> Aabb<T>
{
    if constexpr (std::numeric_limits<T>::has_infinity) {
        auto inf = std::numeric_limits<T>::infinity();
        return {{-inf, -inf}, {inf, inf}, minmax};
    } else {
        auto min = std::numeric_limits<T>::lowest();
        auto max = std::numeric_limits<T>::max();
        return {{min, min}, {max, max}, minmax};
    }
}

template<typename T>
auto aabb_is_empty(const Aabb<T>& aabb) -> bool
{
    return aabb.min.x >= aabb.max.x || aabb.min.y >= aabb.max.y;
}

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
    return rect_cast<Out>(Rect<In>{ origin, extent, xywh });
}
