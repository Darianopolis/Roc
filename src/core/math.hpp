#pragma once

#include "types.hpp"

// -----------------------------------------------------------------------------

constexpr vec2f64 core_copysign(     vec2f64 v, vec2f64 s) { return vec2f64(std::copysign(v.x, s.x), std::copysign(v.y, s.y)); }
constexpr vec2f64 core_round_to_zero(vec2f64 v)            { return core_copysign(glm::floor(glm::abs(v)), v);                 }

// -----------------------------------------------------------------------------

template<typename T> std::string core_to_string(const core_vec<2, T>& vec) { return std::format("({}, {})",         vec.x, vec.y);               }
template<typename T> std::string core_to_string(const core_vec<3, T>& vec) { return std::format("({}, {}, {})",     vec.x, vec.y, vec.z);        }
template<typename T> std::string core_to_string(const core_vec<4, T>& vec) { return std::format("({}, {}, {}, {})", vec.x, vec.y, vec.z, vec.w); }

template<typename T>
std::string core_to_string(const core_rect<T>& rect)
{
    return std::format("(({}, {}) : ({}, {}))", rect.origin.x, rect.origin.y, rect.extent.x, rect.extent.y);
}

template<typename T>
std::string core_to_string(const core_aabb<T>& aabb)
{
    return std::format("(({}, {}) < ({}, {}))", aabb.min.x, aabb.min.y, aabb.max.x, aabb.max.y);
}

// -----------------------------------------------------------------------------

template<typename T>
core_vec<2, T> core_aabb_clamp_point(const core_aabb<T>& rect, core_vec<2, T> point)
{
    return glm::clamp(point, rect.min, rect.max);
}

template<typename T>
bool core_aabb_contains(const core_aabb<T>& rect, core_vec<2, T> point)
{
    return point.x >= rect.min.x && point.x < rect.max.x
        && point.y >= rect.min.y && point.y < rect.max.y;
}

template<typename T>
core_aabb<T> core_aabb_outer(const core_aabb<T>& a, const core_aabb<T>& b)
{
    return {glm::min(a.min, b.min), glm::max(a.max, b.max), core_minmax};
}

template<typename T>
core_aabb<T> core_aabb_inner(const core_aabb<T>& a, const core_aabb<T>& b)
{
    return {glm::max(a.min, b.min), glm::min(a.max, b.max), core_minmax};
}

template<typename T>
bool core_aabb_intersects(const core_aabb<T>& a, const core_aabb<T>& b, core_aabb<T>* intersection = nullptr)
{
    auto i = core_aabb_inner(a, b);

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
u32 core_aabb_subtract(const core_aabb<T>& minuend, const core_aabb<T>& subtrahend, core_aabb<T>* out)
{
    core_aabb<T> intersection;
    if (core_aabb_intersects(minuend, subtrahend, &intersection)) {
        u32 count = 0;
        if (minuend.min.x != intersection.min.x) /* left   */ out[count++] = {{     minuend.min.x, intersection.min.y}, {intersection.min.x, intersection.max.y}, core_minmax};
        if (minuend.max.x != intersection.max.x) /* right  */ out[count++] = {{intersection.max.x, intersection.min.y}, {     minuend.max.x, intersection.max.y}, core_minmax};
        if (minuend.min.y != intersection.min.y) /* top    */ out[count++] = {{     minuend.min},                       {     minuend.max.x, intersection.min.y}, core_minmax};
        if (minuend.max.y != intersection.max.y) /* bottom */ out[count++] = {{     minuend.min.x, intersection.max.y}, {     minuend.max                      }, core_minmax};
        return count;
    } else {
        *out = minuend;
        return 1;
    }
}

// -----------------------------------------------------------------------------

template<typename T>
core_vec<2, T> core_rect_clamp_point(const core_rect<T>& rect, core_vec<2, T> point)
{
    return core_aabb_clamp_point<T>(rect, point);
}

template<typename T>
bool core_rect_contains(const core_rect<T>& rect, core_vec<2, T> point)
{
    return core_aabb_contains<T>(rect, point);
}

template<typename T>
bool core_rect_intersects(const core_rect<T>& a, const core_rect<T>& b, core_rect<T>* intersection = nullptr)
{
    core_aabb<T> i;
    bool intersects = core_aabb_intersects<T>(a, b, &i);
    if (intersection) *intersection = i;
    return intersects;
}

template<typename T>
core_rect<T> core_rect_constrain(core_rect<T> rect, const core_rect<T>& bounds)
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
core_rect<T> core_rect_fit(core_vec<2, T> outer, core_vec<2, T> inner)
{
    T scale = glm::min(outer.x / inner.x, outer.y / inner.y);
    auto extent = inner * scale;
    auto offset = (outer - extent) / T(2);
    return {offset, extent, core_xywh};
}

// -----------------------------------------------------------------------------

template<typename Out, typename In>
core_vec<2, Out> core_round(core_vec<2, In> pos, core_vec<2, In>* remainder = nullptr)
{
    // For points, we floor to treat the position as any point within a given integer region
    auto rounded = glm::floor(pos);
    if (remainder) *remainder = pos - rounded;
    return rounded;
}

template<typename Out, typename In>
core_rect<Out> core_round(core_rect<In> rect, core_rect<In>* remainder = nullptr)
{
    core_aabb<In> bounds = rect;
    auto min = bounds.min;
    auto max = bounds.max;
    // For rects, we round as the min and max are treated as integer boundaries
    auto extent = glm::round(max - min);
    auto origin = glm::round(min);
    if (remainder) {
        *remainder = {
            min - origin,
            max - min - (extent),
            core_xywh,
        };
    }
    return { origin, extent, core_xywh };
}
