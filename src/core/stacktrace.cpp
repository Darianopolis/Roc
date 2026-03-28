#include "stacktrace.hpp"
#include "debug.hpp"

std::pair<const Stacktrace*, bool> StacktraceCache::insert(const std::stacktrace& st)
{
    auto& entry = traces[st];
    bool added = entry.size() != st.size();
    if (added) {
        entry.populate(*this, st);
    }
    return {&entry, added};
}

void Stacktrace::populate(struct StacktraceCache& cache, const std::stacktrace& stacktrace)
{
    debug_assert(entries.empty());

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
