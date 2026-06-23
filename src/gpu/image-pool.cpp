#include "internal.hpp"

struct GpuImagePattern;

struct GpuDefaultImagePool : GpuImagePool
{
    Gpu* gpu;

    std::vector<GpuImagePattern*> patterns;

    virtual auto acquire(const GpuImageCreateInfo&) -> Ref<GpuImage> final override;

    ~GpuDefaultImagePool();
};

struct GpuImagePattern
{
    GpuDefaultImagePool* pool;

    GpuFormatModifierSet modifiers;
    GpuImageCreateInfo info;
    RefVector<GpuImage> images;

    ~GpuImagePattern()
    {
        if (pool) std::erase(pool->patterns, this);
    }
};

GpuDefaultImagePool::~GpuDefaultImagePool()
{
    for (auto* pattern : patterns) {
        pattern->pool = nullptr;
    }
}

auto gpu_image_pool_create(Gpu* gpu) -> Ref<GpuImagePool>
{
    auto pool = ref_create<GpuDefaultImagePool>();
    pool->gpu = gpu;
    return pool;
}

static
auto find_pattern(GpuDefaultImagePool* pool, const GpuImageCreateInfo& info) -> Ref<GpuImagePattern>
{
    for (auto& pattern : pool->patterns) {
        if (pattern->info.extent != info.extent) continue;
        if (pattern->info.format != info.format) continue;
        if (pattern->info.usage  != info.usage)  continue;
        if (pattern->info.flags  != info.flags)  continue;
        if (bool(pattern->info.modifiers) != bool(info.modifiers)) continue;
        if (info.modifiers && *pattern->info.modifiers != *info.modifiers) continue;

        return pattern;
    }

    auto pattern = ref_create<GpuImagePattern>();
    pattern->pool = pool;
    pattern->info = info;
    if (info.modifiers) {
        pattern->modifiers = *info.modifiers;
        pattern->info.modifiers = &pattern->modifiers;
    }
    pool->patterns.emplace_back(pattern.get());

    return pattern;
}

static
auto make_lease(GpuImagePattern* pattern, Ref<GpuImage> image)
{
    return gpu_lease_image(std::move(image), [pattern = Ref(pattern)](Ref<GpuImage> image) {
        pattern->images.emplace_back(std::move(image));
    });
}

auto GpuDefaultImagePool::acquire(const GpuImageCreateInfo& info) -> Ref<GpuImage>
{
    auto pattern = find_pattern(this, info);

    if (!pattern->images.empty()) {
        return make_lease(pattern.get(), pattern->images.pop_back());
    }

    auto image = gpu_image_create(gpu, info);

    return make_lease(pattern.get(), std::move(image));
}
