#pragma once

#include "types.hpp"
#include "math.hpp"

namespace core
{
    template<typename T>
    struct Region
    {
        std::vector<core::Aabb<T>> aabbs;

// -----------------------------------------------------------------------------

        Region() = default;

        Region(core::Aabb<T> aabb)
            : aabbs{aabb}
        {}

// -----------------------------------------------------------------------------

        Region(const Region& other)
            : aabbs(other.aabbs)
        {}

        Region& operator=(const Region& other)
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

        Region& operator=(Region&& other)
        {
            if (this != &other) {
                aabbs = std::move(other.aabbs);
            }
            return *this;
        }

// -----------------------------------------------------------------------------

        constexpr bool operator==(const Region& other) const noexcept = default;

// -----------------------------------------------------------------------------

        void clear()
        {
            aabbs.clear();
        }

        bool empty() const
        {
            return aabbs.empty();
        }

        void add(core::Aabb<T> aabb)
        {
            aabbs.emplace_back(aabb);
        }

        void subtract(core::Aabb<T> subtrahend)
        {
            usz prev_size = aabbs.size();
            usz inplace = 0;

            for (usz i = 0; i < prev_size; ++i) {

                std::array<core::Aabb<T>, 4> split;
                u32 count = core::aabb::subtract(aabbs[i], subtrahend, split.data());

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
        bool contains(core::Vec<2, T2> point) const
        {
            for (auto aabb : aabbs) {
                if (core::aabb::contains<T2>(aabb, point)) {
                    return true;
                }
            }
            return false;
        }

        template<typename T2>
        bool contains(core::Aabb<T2> needle) const
        {
            for (auto aabb : aabbs) {
                core::Aabb<T2> overlap;
                core::aabb::intersects<T2>(aabb, needle, &overlap);
                if (overlap == needle) return true;
            }
            return false;
        }

        template<typename T2>
        core::Vec<2, T2> constrain(core::Vec<2, T2> point) const
        {
            f64 closest_dist = INFINITY;
            core::Vec<2, T2> closest = {};

            for (auto aabb : aabbs) {
                auto pos = core::aabb::clamp_point<T2>(aabb, point);
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
}

// -----------------------------------------------------------------------------

using region2i32 = core::Region<i32>;
using region2f32 = core::Region<f32>;
