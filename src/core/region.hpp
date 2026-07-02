#pragma once

#include "types.hpp"
#include "math.hpp"

template<typename T>
struct Region
{
    std::vector<Aabb<T>> aabbs;

// -----------------------------------------------------------------------------

    Region() = default;

    Region(Aabb<T> aabb)
        : aabbs{aabb}
    {}

// -----------------------------------------------------------------------------

    Region(const Region& other)
        : aabbs(other.aabbs)
    {}

    auto& operator=(const Region& other)
    {
        if (this != &other) {
            aabbs = other.aabbs;
        }
        return *this;
    }

// -----------------------------------------------------------------------------

    Region(Region&& other)
        : aabbs(std::move(other.aabbs))
    {}

    auto& operator=(Region&& other)
    {
        if (this != &other) {
            aabbs = std::move(other.aabbs);
        }
        return *this;
    }

// -----------------------------------------------------------------------------

    constexpr auto operator==(const Region& other) const noexcept -> bool = default;

// -----------------------------------------------------------------------------

    void clear()
    {
        aabbs.clear();
    }

    auto empty() const -> bool
    {
        return aabbs.empty();
    }

    void add(Aabb<T> addition)
    {
        if (addition.max.x - addition.min.x <= 0) return;
        if (addition.max.y - addition.min.y <= 0) return;
        for (Aabb<T>& aabb : aabbs) {
            if (aabb.min.x == addition.min.x && aabb.max.x == addition.max.x) {
                aabb.min.y = std::min(aabb.min.y, addition.min.y);
                aabb.max.y = std::max(aabb.max.y, addition.max.y);
                return;
            } else if (aabb.min.y == addition.min.y && aabb.max.y == addition.max.y) {
                aabb.min.x = std::min(aabb.min.x, addition.min.x);
                aabb.max.x = std::max(aabb.max.x, addition.max.x);
                return;
            }
        }
        aabbs.emplace_back(addition);
    }

    void subtract(Aabb<T> subtrahend)
    {
        usz prev_size = aabbs.size();
        usz inplace = 0;

        for (usz i = 0; i < prev_size; ++i) {
            std::array<Aabb<T>, 4> split;
            u32 count = aabb_subtract(aabbs[i], subtrahend, split.data());

            // Update first aabb in-place
            if (count > 0) {
                aabbs[inplace++] = split[0];
            }

            // Append remaining aabbs to end
            if (count > 1) {
                aabbs.insert_range(aabbs.end(), std::span{split}.subspan(1, count - 1));
            }
        }

        // Clean if hole left in list
        if (inplace < prev_size) {
            usz extra = aabbs.size() - prev_size;
            if (extra) {
                memmove(aabbs.data() + inplace, aabbs.data() + prev_size, extra * sizeof(Aabb<T>));
            }
            aabbs.erase(aabbs.begin() + inplace + extra, aabbs.end());
        }
    }

    template<typename T2>
    auto contains(Vec<2, T2> point) const -> bool
    {
        for (auto aabb : aabbs) {
            if (aabb_contains<T2>(aabb, point)) {
                return true;
            }
        }
        return false;
    }

    template<typename T2>
    auto contains(Aabb<T2> needle) const -> bool
    {
        for (auto aabb : aabbs) {
            Aabb<T2> overlap;
            aabb_intersects<T2>(aabb, needle, &overlap);
            if (overlap == needle) return true;
        }
        return false;
    }

    template<typename T2>
    auto constrain(Vec<2, T2> point) const -> Vec<2, T2>
    {
        f64 closest_dist = INFINITY;
        Vec<2, T2> closest = {};

        for (auto aabb : aabbs) {
            auto pos = aabb_clamp_point(aabb_cast<T2>(aabb), point);
            if (pos == point) return point;

            f64 dist = vec_distance(vec_cast<f64>(pos), vec_cast<f64>(point));
            if (dist < closest_dist) {
                closest = pos;
                closest_dist = dist;
            }
        }

        return closest;
    }

    auto bounds() const -> Aabb<T>
    {
        Aabb<T> bounds = Aabb<T>::empty();
        for (auto& aabb : aabbs) {
            bounds = aabb_outer(bounds, aabb);
        }
        return bounds;
    }
};

// -----------------------------------------------------------------------------

using region2i32 = Region<i32>;
using region2f32 = Region<f32>;
