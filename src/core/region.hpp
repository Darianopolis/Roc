#pragma once

#include "types.hpp"
#include "math.hpp"
#include "debug.hpp"

template<typename T>
struct RegionSection
{
    T min;
    T max;

    constexpr
    auto operator==(const RegionSection&) const noexcept -> bool = default;
};

template<typename T>
struct RegionBand
{
    T min;
    T max;
    u32 start;
    u32 count;

    constexpr
    auto operator==(const RegionBand&) const noexcept -> bool = default;
};

enum class RegionOp
{
    merge,
    subtract,
    intersect,
};

template<typename T, template<typename C> typename Container = std::vector>
struct Region
{
    Container<RegionSection<T>> sections;
    Container<RegionBand<T>>    bands;

    // Invariants
    //  - Bands are never empty                             Band::count > 0
    //  - Bands are ordered                                 bands[N].min >= bands[N-1].min
    //  - Sections are never empty                          Section::max > Section::min
    //  - Sections are ordered and disjoint within a band   sections[N].min > sections[N-1].max
    //  - Bands always refer to a valid span of sections    Band::start + Band::count < sections.size()

    constexpr
    Region() = default;

    constexpr
    Region(Aabb<T> aabb)
    {
        if (aabb.min.x < aabb.max.x && aabb.min.y < aabb.max.y) {
            sections.emplace_back(aabb.min.x, aabb.max.x);
            bands.emplace_back(aabb.min.y, aabb.max.y, 0, 1);
        }
    }

    constexpr
    void clear()
    {
        bands.clear();
        sections.clear();
    }

    // Queries

    constexpr
    auto operator==(const Region&) const noexcept -> bool = default;

    constexpr
    auto empty() const noexcept -> bool
    {
        return bands.empty();
    }

    constexpr
    auto bounds() const noexcept -> Aabb<T>
    {
        auto bounds = Aabb<T>::empty();
        if (empty()) return bounds;
        bounds.min.y = bands.front().min;
        bounds.max.y = bands.back().max;
        for (auto& band : bands) {
            bounds.min.x = std::min(bounds.min.x, sections[band.start].min);
            bounds.max.x = std::max(bounds.max.x, sections[band.start + band.count - 1].max);
        }
        return bounds;
    }

    template<typename T2>
    constexpr
    auto contains(Vec<2, T2> point) const -> bool
    {
        for (auto& band : bands) {
            if (point.y < band.min || point.y >= band.max) continue;
            for (auto& section : std::span(sections).subspan(band.start, band.count)) {
                if (point.x >= section.min && point.x < section.max) return true;
            }
        }
        return false;
    }

    template<typename T2>
    constexpr
    auto contains(Aabb<T2> needle) const -> bool
    {
        for (auto& band : bands) {
            if (needle.min.y < band.min || needle.max.y >= band.max) continue;
            for (auto& section : std::span(sections).subspan(band.start, band.count)) {
                if (needle.min.x >= section.min && needle.max.x < section.max) return true;
            }
        }
        return false;
    }

    template<typename T2>
    constexpr
    auto constrain(Vec<2, T2> point) const -> Vec<2, T2>
    {
        f64 closest_dist = INFINITY;
        Vec<2, T2> closest = {};

        for (auto& band : bands) {
            for (auto& section : std::span(sections).subspan(band.start, band.count)) {
                Aabb<T> aabb{{section.min, band.min}, {section.max, band.max}, minmax};
                auto pos = aabb_clamp_point(aabb_cast<T2>(aabb), point);
                if (pos == point) return point;

                f64 dist = vec_distance(vec_cast<f64>(pos), vec_cast<f64>(point));
                if (dist < closest_dist) {
                    closest = pos;
                    closest_dist = dist;
                }
            }
        }

        return closest;
    }

    // Convenience

    constexpr
    void add(Aabb<T> addition)
    {
        Region<T> out;

        RegionSection<T> section{addition.min.x, addition.max.x};
        RegionBand<T> band{addition.min.y, addition.max.y, 0, 1};
        Region<T, std::span> temp;
        temp.sections = std::span(&section, 1);
        temp.bands = std::span(&band, 1);

        region_op(out, *this, temp, RegionOp::merge);
        *this = std::move(out);
    }

    constexpr
    void subtract(Aabb<T> addition)
    {
        Region<T> out;

        RegionSection<T> section{addition.min.x, addition.max.x};
        RegionBand<T> band{addition.min.y, addition.max.y, 0, 1};
        Region<T, std::span> temp;
        temp.sections = std::span(&section, 1);
        temp.bands = std::span(&band, 1);

        region_op(out, *this, temp, RegionOp::subtract);
        *this = std::move(out);
    }
};

template<typename T, template<typename C> typename CO, template<typename C> typename CA, template<typename C> typename CB>
constexpr
void region_op(Region<T, CO>& out, const Region<T, CA>& region_a, const Region<T, CB>& region_b, RegionOp op)
{
    if constexpr (std::same_as<Region<T, CO>, Region<T, CA>>) debug_assert(&out != &region_a);
    if constexpr (std::same_as<Region<T, CO>, Region<T, CB>>) debug_assert(&out != &region_b);

    out.bands.clear();
    out.sections.clear();

    static constexpr auto for_each_subrange = [](const auto& as, const auto& bs, auto&& f) {
        if (as.empty() && bs.empty()) return;

        auto a = as.begin();
        auto b = bs.begin();

        T cur = as.empty() ? b->min : (bs.empty() ? a->min : std::min(a->min, b->min));

        for (;;) {
            T a_next = cur;
            for (; a != as.end(); ++a) {
                if (a->min > cur) { a_next = a->min; break; }
                if (a->max > cur) { a_next = a->max; break; }
            }

            T b_next = cur;
            for (; b != bs.end(); ++b) {
                if (b->min > cur) { b_next = b->min; break; }
                if (b->max > cur) { b_next = b->max; break; }
            }

            if (a_next == cur && b_next == cur) break;
            T next = a_next == cur ? b_next : (b_next == cur ? a_next : std::min(a_next, b_next));

            f(cur, next, (a != as.end() && a->min < next) ? &*a : nullptr,
                         (b != bs.end() && b->min < next) ? &*b : nullptr);
            cur = next;
        }
    };

    for_each_subrange(
        region_a.bands,
        region_b.bands,
        [&](T min, T max, const RegionBand<T>* band_a, const RegionBand<T>* band_b) {
            auto old_section_offset = out.sections.size();

            for_each_subrange(
                band_a ? std::span(region_a.sections).subspan(band_a->start, band_a->count) : std::span<RegionSection<T>>(),
                band_b ? std::span(region_b.sections).subspan(band_b->start, band_b->count) : std::span<RegionSection<T>>(),
                [&](T begin, T end, const RegionSection<T>* sec_a, const RegionSection<T>* sec_b) {
                    bool include = false;
                    switch (op) {
                        break;case RegionOp::merge:     include = sec_a ||  sec_b;
                        break;case RegionOp::subtract:  include = sec_a && !sec_b;
                        break;case RegionOp::intersect: include = sec_a &&  sec_b;
                    }
                    if (!include) return;

                    if (old_section_offset == out.sections.size() || out.sections.back().max != begin) {
                        // Append
                        out.sections.emplace_back(begin, end);
                    } else {
                        // Merge
                        out.sections.back().max = end;
                    }
                });

            // Empty
            if (old_section_offset == out.sections.size()) return;

            // Merge
            if (!out.bands.empty()) {
                auto& last_band = out.bands.back();
                if (last_band.max == min && std::ranges::equal(
                    std::span(out.sections).subspan(last_band.start, last_band.count),
                    std::span(out.sections).subspan(old_section_offset)
                )) {
                    last_band.max = max;
                    out.sections.resize(old_section_offset);
                    return;
                }
            }

            // Append
            out.bands.emplace_back(min, max, old_section_offset, out.sections.size() - old_section_offset);
        });
}

// -----------------------------------------------------------------------------

using region2i32 = Region<i32>;
using region2f32 = Region<f32>;
