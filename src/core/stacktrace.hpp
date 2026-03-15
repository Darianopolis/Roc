#pragma once

#include "types.hpp"
#include "containers.hpp"

namespace core
{
    struct StacktraceEntryData
    {
        bool populated = false;
        std::string description;
        std::filesystem::path source_file;
        u32 source_line;
    };

    struct StacktraceEntry
    {
        const core::StacktraceEntryData* data;

        const std::string& description() const noexcept { return data->description; }
        const std::filesystem::path& source_file() const noexcept { return data->source_file; }
        u32 source_line() const noexcept { return data->source_line; }
    };

    struct StacktraceCache;

    struct Stacktrace
    {
        std::vector<core::StacktraceEntry> entries;

        Stacktrace() = default;

        void populate(struct core::StacktraceCache& cache, const std::stacktrace& stacktrace);

        usz size() const noexcept { return entries.size(); }
        StacktraceEntry at(usz i) const { return entries.at(i); }

        auto begin() const noexcept { return entries.begin(); }
        auto end() const noexcept { return entries.end(); }
    };

    std::string to_string(const core::Stacktrace& st);

    struct StacktraceCache
    {
        core::SegmentedMap<std::stacktrace_entry, core::StacktraceEntryData> entries;
        core::SegmentedMap<std::stacktrace, core::Stacktrace> traces;

        std::pair<const core::Stacktrace*, bool> insert(const std::stacktrace& st);
    };
}
