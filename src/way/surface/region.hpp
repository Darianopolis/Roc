#pragma once

#include "../util.hpp"

#include <core/region.hpp>
#include <core/math.hpp>

// -----------------------------------------------------------------------------

struct WayRegion : WayObject
{
    WayResource resource;

    region2f32 region;
};

// -----------------------------------------------------------------------------

/**
 * Represents accumulated damage for a surface or buffer.
 *
 * Accurate damage is not required for correctness, as reading from un-damaged parts of a buffer is valid.
 *
 * Taking advantage of this, and the fact that damage is usually localized, we only
 * track the outer bounds of accumulated damage. This avoids relatively expensive region operations,
 * and reduces the number of copies on transfer and clip regions during rendering; while remaining
 * optimal or near optimal for all realistic scenarios.
 */
struct WayDamageRegion
{
    static constexpr aabb2i32 Empty = {{INT_MAX, INT_MAX}, {INT_MIN, INT_MIN}, minmax};

private:
    aabb2i32 region = Empty;

public:
    void damage(aabb2i32 damage)
    {
        region = aabb_outer(region, damage);
    }

    void clip_to(aabb2i32 limit)
    {
        region = aabb_inner(region, limit);
    }

    void clear()
    {
        region = Empty;
    }

    explicit operator bool() const
    {
        return region.max.x > region.min.x
            && region.max.y > region.min.y;
    }

    auto bounds() -> aabb2i32
    {
        debug_assert(*this);
        return region;
    }
};
