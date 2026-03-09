#pragma once

#include "util.hpp"

template<typename T>
struct core_region
{
    std::vector<core_aabb<T>> aabbs;

// -----------------------------------------------------------------------------

    core_region() = default;

    core_region(core_aabb<T> aabb)
        : aabbs{aabb}
    {}

// -----------------------------------------------------------------------------

    core_region(const core_region& other)
        : aabbs(other.aabbs)
    {}

    core_region& operator=(const core_region& other)
    {
        if (this != &other) {
            aabbs = other.aabbs;
        }
        return *this;
    }

// -----------------------------------------------------------------------------

    core_region(core_region&& other)
        : aabbs(std::move(other.aabbs))
    {}

    core_region& operator=(core_region&& other)
    {
        if (this != &other) {
            aabbs = std::move(other.aabbs);
        }
        return *this;
    }

// -----------------------------------------------------------------------------

    constexpr bool operator==(const core_region& other) const noexcept = default;

// -----------------------------------------------------------------------------

    void clear()
    {
        aabbs.clear();
    }

    bool empty() const
    {
        return aabbs.empty();
    }

    void add(core_aabb<T> aabb)
    {
        aabbs.emplace_back(aabb);
    }

    void subtract(core_aabb<T> subtrahend)
    {
        usz prev_size = aabbs.size();
        usz inplace = 0;

        for (usz i = 0; i < prev_size; ++i) {

            std::array<core_aabb<T>, 4> split;
            u32 count = core_aabb_subtract(aabbs[i], subtrahend, split.data());

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
    bool contains(core_vec<2, T2> point) const
    {
        for (auto aabb : aabbs) {
            if (core_aabb_contains<T2>(aabb, point)) {
                return true;
            }
        }
        return false;
    }

    template<typename T2>
    bool contains(core_aabb<T2> needle) const
    {
        for (auto aabb : aabbs) {
            core_aabb<T2> overlap;
            core_aabb_intersects<T2>(aabb, needle, &overlap);
            if (overlap == needle) return true;
        }
        return false;
    }

    template<typename T2>
    core_vec<2, T2> constrain(core_vec<2, T2> point) const
    {
        f64 closest_dist = INFINITY;
        core_vec<2, T2> closest = {};

        for (auto aabb : aabbs) {
            auto pos = core_aabb_clamp_point<T2>(aabb, point);
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

using region2i32 = core_region<i32>;
using region2f32 = core_region<f32>;
