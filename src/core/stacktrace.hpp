#pragma once

#include "types.hpp"

struct StacktraceEntryData
{
    bool populated = false;
    std::string description;
    std::filesystem::path source_file;
    u32 source_line;
};

struct StacktraceEntry
{
    const StacktraceEntryData* data;

    auto description() const noexcept -> const std::string&           { return data->description; }
    auto source_file() const noexcept -> const std::filesystem::path& { return data->source_file; }
    auto source_line() const noexcept -> u32                          { return data->source_line; }
};

struct Stacktrace
{
    std::vector<StacktraceEntry> entries;

    Stacktrace() = default;

    void populate(struct StacktraceCache& cache, const std::stacktrace& stacktrace);

    usz size() const noexcept { return entries.size(); }
    StacktraceEntry at(usz i) const { return entries.at(i); }

    auto begin() const noexcept { return entries.begin(); }
    auto end() const noexcept { return entries.end(); }
};

template<>
struct std::formatter<Stacktrace> {
    constexpr auto parse(auto& ctx) { return ctx.begin(); }
    constexpr auto format(const Stacktrace& st, auto& ctx) const
    {
        bool first = true;
        auto out = ctx.out();
        for (u32 i = 0; i < st.entries.size(); ++i) {
            if (!std::exchange(first, false)) out = std::format_to(out, "\n");
            auto& e = st.entries[i];
            out = std::format_to(out, "{:4}# {:4} at {}:{}", i, e.description(), e.source_file().c_str(), e.source_line());
        }
        return ctx.out();
    }
};

struct StacktraceCache
{
    ankerl::unordered_dense::segmented_map<std::stacktrace_entry, StacktraceEntryData> entries;
    ankerl::unordered_dense::segmented_map<std::stacktrace, Stacktrace> traces;

    auto insert(const std::stacktrace& st) -> std::pair<const Stacktrace*, bool>;
};
