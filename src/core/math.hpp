#pragma once

#include "types.hpp"

// -----------------------------------------------------------------------------

namespace core
{
    constexpr vec2f64 copysign(vec2f64 v, vec2f64 s)
    {
        return vec2f64(std::copysign(v.x, s.x), std::copysign(v.y, s.y));
    }

    constexpr vec2f64 round_to_zero(vec2f64 v)
    {
        return core::copysign(glm::floor(glm::abs(v)), v);
    }
}

// -----------------------------------------------------------------------------

namespace core
{
    template<typename T> auto to_string(const core::Vec<2, T>& vec) -> std::string { return std::format("({}, {})",         vec.x, vec.y);               }
    template<typename T> auto to_string(const core::Vec<3, T>& vec) -> std::string { return std::format("({}, {}, {})",     vec.x, vec.y, vec.z);        }
    template<typename T> auto to_string(const core::Vec<4, T>& vec) -> std::string { return std::format("({}, {}, {}, {})", vec.x, vec.y, vec.z, vec.w); }

    template<typename T>
    auto to_string(const core::Rect<T>& rect) -> std::string
    {
        return std::format("(({}, {}) : ({}, {}))", rect.origin.x, rect.origin.y, rect.extent.x, rect.extent.y);
    }

    template<typename T>
    auto to_string(const core::Aabb<T>& aabb) -> std::string
    {
        return std::format("(({}, {}) < ({}, {}))", aabb.min.x, aabb.min.y, aabb.max.x, aabb.max.y);
    }
}

// -----------------------------------------------------------------------------

namespace core::aabb
{
    template<typename T>
    auto clamp_point(const core::Aabb<T>& rect, core::Vec<2, T> point) -> core::Vec<2, T>
    {
        return glm::clamp(point, rect.min, rect.max);
    }

    template<typename T>
    bool contains(const core::Aabb<T>& rect, core::Vec<2, T> point)
    {
        return point.x >= rect.min.x && point.x < rect.max.x
            && point.y >= rect.min.y && point.y < rect.max.y;
    }

    template<typename T>
    auto outer(const core::Aabb<T>& a, const core::Aabb<T>& b) -> core::Aabb<T>
    {
        return {glm::min(a.min, b.min), glm::max(a.max, b.max), core::minmax};
    }

    template<typename T>
    auto inner(const core::Aabb<T>& a, const core::Aabb<T>& b) -> core::Aabb<T>
    {
        return {glm::max(a.min, b.min), glm::min(a.max, b.max), core::minmax};
    }

    template<typename T>
    bool intersects(const core::Aabb<T>& a, const core::Aabb<T>& b, core::Aabb<T>* intersection = nullptr)
    {
        auto i = core::aabb::inner(a, b);

        if (i.max.x <= i.min.x || i.max.y <= i.min.y) {
            if (intersection) *intersection = {};
            return false;
        } else {
            if (intersection) *intersection = i;
            return true;
        }
    }

    template<typename T>
    u32  subtract(const core::Aabb<T>& minuend, const core::Aabb<T>& subtrahend, core::Aabb<T>* out)
    {
        core::Aabb<T> intersection;
        if (core::aabb::intersects(minuend, subtrahend, &intersection)) {
            u32 count = 0;
            if (minuend.min.x != intersection.min.x) /* left   */ out[count++] = {{     minuend.min.x, intersection.min.y}, {intersection.min.x, intersection.max.y}, core::minmax};
            if (minuend.max.x != intersection.max.x) /* right  */ out[count++] = {{intersection.max.x, intersection.min.y}, {     minuend.max.x, intersection.max.y}, core::minmax};
            if (minuend.min.y != intersection.min.y) /* top    */ out[count++] = {{     minuend.min},                       {     minuend.max.x, intersection.min.y}, core::minmax};
            if (minuend.max.y != intersection.max.y) /* bottom */ out[count++] = {{     minuend.min.x, intersection.max.y}, {     minuend.max                      }, core::minmax};
            return count;
        } else {
            *out = minuend;
            return 1;
        }
    }
}

// -----------------------------------------------------------------------------

namespace core::rect
{
    template<typename T>
    core::Vec<2, T> clamp_point(const core::Rect<T>& rect, core::Vec<2, T> point)
    {
        return core::aabb::clamp_point<T>(rect, point);
    }

    template<typename T>
    bool contains(const core::Rect<T>& rect, core::Vec<2, T> point)
    {
        return core::aabb::contains<T>(rect, point);
    }

    template<typename T>
    bool intersects(const core::Rect<T>& a, const core::Rect<T>& b, core::Rect<T>* intersection = nullptr)
    {
        core::Aabb<T> i;
        bool intersects = core::aabb::intersects<T>(a, b, &i);
        if (intersection) *intersection = i;
        return intersects;
    }

    template<typename T>
    core::Rect<T> constrain(core::Rect<T> rect, const core::Rect<T>& bounds)
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

    template<typename T>
    core::Rect<T> fit(core::Vec<2, T> outer, core::Vec<2, T> inner)
    {
        T scale = glm::min(outer.x / inner.x, outer.y / inner.y);
        auto extent = inner * scale;
        auto offset = (outer - extent) / T(2);
        return {offset, extent, core::xywh};
    }
}

// -----------------------------------------------------------------------------

namespace core
{
    template<typename Out, typename In>
    core::Vec<2, Out> round(core::Vec<2, In> pos, core::Vec<2, In>* remainder = nullptr)
    {
        // For points, we floor to treat the position as any point within a given integer region
        auto rounded = glm::floor(pos);
        if (remainder) *remainder = pos - rounded;
        return rounded;
    }

    template<typename Out, typename In>
    core::Rect<Out> round(core::Rect<In> rect, core::Rect<In>* remainder = nullptr)
    {
        core::Aabb<In> bounds = rect;
        auto min = bounds.min;
        auto max = bounds.max;
        // For rects, we round as the min and max are treated as integer boundaries
        auto extent = glm::round(max - min);
        auto origin = glm::round(min);
        if (remainder) {
            *remainder = {
                min - origin,
                max - min - (extent),
                core::xywh,
            };
        }
        return { origin, extent, core::xywh };
    }
}
