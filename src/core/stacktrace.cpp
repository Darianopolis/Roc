#include "stacktrace.hpp"
#include "debug.hpp"

std::string core::to_string(const core::Stacktrace& st)
{
    std::string str;
    for (u32 i = 0; i < st.entries.size(); ++i) {
        if (!str.empty()) str += '\n';
        auto& e = st.entries[i];
        str += std::format("{:4}# {:4} at {}:{}", i, e.description(), e.source_file().c_str(), e.source_line());
    }
    return str;
}

std::pair<const core::Stacktrace*, bool> core::StacktraceCache::insert(const std::stacktrace& st)
{
    auto& entry = traces[st];
    bool added = entry.size() != st.size();
    if (added) {
        entry.populate(*this, st);
    }
    return {&entry, added};
}

void core::Stacktrace::populate(struct core::StacktraceCache& cache, const std::stacktrace& stacktrace)
{
    core_assert(entries.empty());

    entries.reserve(stacktrace.size());
    for (auto& e : stacktrace) {
        auto& cached = cache.entries[e];
        if (!cached.populated) {
            cached.description = e.description();
            cached.source_file = e.source_file();

            if (!cached.source_file.empty()) {
                try {
                    cached.source_file = std::filesystem::canonical(cached.source_file);
                } catch (...) {
                    // Fall-back to lexically normal
                    cached.source_file = cached.source_file.lexically_normal();
                }
            }

            cached.source_line = e.source_line();
            cached.populated = true;
        }

        entries.emplace_back(&cached);
    }
}
