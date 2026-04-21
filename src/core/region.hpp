#pragma once

#include "types.hpp"

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

    void add(Aabb<T> aabb)
    {
        aabbs.emplace_back(aabb);
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
            usz to_delete = prev_size - inplace;
            usz to_move = std::min(to_delete, extra);
            std::copy(aabbs.end() - to_move, aabbs.end(), aabbs.begin() + inplace);
            aabbs.erase(aabbs.begin() + inplace + extra);
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
            auto pos = aabb_clamp_point<T2>(aabb, point);
            if (pos == point) return point;

            f64 dist = glm::distance(vec2f64(pos), vec2f64(point));
            if (dist < closest_dist) {
                closest = pos;
                closest_dist = dist;
            }
        }

        return closest;
    }
};

// -----------------------------------------------------------------------------

using region2i32 = Region<i32>;
using region2f32 = Region<f32>;
