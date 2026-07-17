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

template<typename T, typename S = std::vector<RegionSection<T>>, typename B = std::vector<RegionBand<T>>>
struct Region
{
    using scalar_type = T;

    S sections;
    B bands;

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
        if (!aabb_is_empty(aabb)) {
            sections = {{aabb.min.x, aabb.max.x}};
            bands = {{aabb.min.y, aabb.max.y, 0, 1}};
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
    auto aabbs() const noexcept
    {
        return bands
            | std::views::transform([&](const auto& band) {
                return std::span(sections).subspan(band.start, band.count)
                    | std::views::transform([&](const auto& section) {
                        return aabb2f32{{section.min, band.min}, {section.max, band.max}, minmax};
                    });
            })
            | std::views::join;
    }

    constexpr
    auto bounds() const noexcept -> Aabb<T>
    {
        auto bounds = aabb_make_empty<T>();
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
    auto intersects(Aabb<T2> needle) const -> bool
    {
        for (auto& band : bands) {
            if (needle.min.y >= band.max || needle.max.y <= band.min) continue;
            for (auto& section : std::span(sections).subspan(band.start, band.count)) {
                if (needle.min.x < section.max && needle.max.x > section.min) return true;
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
};

template<typename T>
using RegionSingle = Region<T, std::optional<RegionSection<T>>, std::optional<RegionBand<T>>>;

using RegionOpUnion     = decltype([](bool a, bool b) constexpr { return a ||  b; });
using RegionOpSubtract  = decltype([](bool a, bool b) constexpr { return a && !b; });
using RegionOpIntersect = decltype([](bool a, bool b) constexpr { return a &&  b; });

template<typename Op, typename RO, typename RA, typename RB>
    requires std::same_as<typename RO::scalar_type, typename RA::scalar_type>
          && std::same_as<typename RA::scalar_type, typename RB::scalar_type>
constexpr
void region_op(RO& out, const RA& region_a, const RB& region_b, const Op& op = Op{})
{
    using T = RO::scalar_type;

    bool in_place = false;
    if constexpr (std::same_as<std::remove_cvref_t<RO>, std::remove_cvref_t<RA>>) in_place |= &out == &region_a;
    if constexpr (std::same_as<std::remove_cvref_t<RO>, std::remove_cvref_t<RB>>) in_place |= &out == &region_b;
    if (in_place) {
        RO tmp = {};
        region_op<Op>(tmp, region_a, region_b);
        out = std::move(tmp);
        return;
    }

    out.bands.clear();
    out.sections.clear();

    static constexpr auto for_each_subrange = [](const auto& as, const auto& bs, auto&& f) {
        bool no_as = as.begin() == as.end();
        bool no_bs = bs.begin() == bs.end();
        if (no_as && no_bs) return;

        auto a = as.begin();
        auto b = bs.begin();

        T cur = no_as ? b->min : (no_bs ? a->min : std::min(a->min, b->min));

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
                    if (!op(sec_a, sec_b)) return;

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

template<typename Op, typename RA, typename RB>
    requires std::same_as<typename RA::scalar_type, typename RB::scalar_type>
constexpr
auto region_op(const RA& region_a, const RB& region_b, const Op& op = Op{}) -> Region<typename RA::scalar_type>
{
    Region<typename RA::scalar_type> out = {};
    region_op<Op>(out, region_a, region_b, op);
    return out;
}
