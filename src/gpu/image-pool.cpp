#include "internal.hpp"

struct gpu_image_pattern;

struct gpu_image_pool_default : gpu_image_pool
{
    gpu_context* gpu;

    std::vector<gpu_image_pattern*> patterns;

    virtual auto acquire(const gpu_image_create_info&) -> core::Ref<gpu_image> final override;

    ~gpu_image_pool_default();
};

struct gpu_image_pattern
{
    gpu_image_pool_default* pool;

    gpu_format_modifier_set     modifiers;
    gpu_image_create_info       info;
    std::vector<core::Ref<gpu_image>> images;

    ~gpu_image_pattern()
    {
        if (pool) std::erase(pool->patterns, this);
    }
};

gpu_image_pool_default::~gpu_image_pool_default()
{
    for (auto* pattern : patterns) {
        pattern->pool = nullptr;
    }
}

auto gpu_image_pool_create(gpu_context* gpu) -> core::Ref<gpu_image_pool>
{
    auto pool = core::create<gpu_image_pool_default>();
    pool->gpu = gpu;
    return pool;
}

static
auto find_pattern(gpu_image_pool_default* pool, const gpu_image_create_info& info) -> core::Ref<gpu_image_pattern>
{
    for (auto& pattern : pool->patterns) {
        if (pattern->info.extent != info.extent) continue;
        if (pattern->info.format != info.format) continue;
        if (pattern->info.usage  != info.usage)  continue;
        if (bool(pattern->info.modifiers) != bool(info.modifiers)) continue;
        if (info.modifiers && *pattern->info.modifiers != *info.modifiers) continue;

        return pattern;
    }

    auto pattern = core::create<gpu_image_pattern>();
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
auto make_lease(gpu_image_pattern* pattern, core::Ref<gpu_image> image)
{
    return gpu_lease_image(std::move(image), [pattern = core::Ref(pattern)](core::Ref<gpu_image> image) {
        pattern->images.emplace_back(std::move(image));
    });
}

auto gpu_image_pool_default::acquire(const gpu_image_create_info& info) -> core::Ref<gpu_image>
{
    auto pattern = find_pattern(this, info);

    if (!pattern->images.empty()) {
        auto image = std::move(pattern->images.back());
        pattern->images.pop_back();
        return make_lease(pattern.get(), std::move(image));
    }

    auto image = gpu_image_create(gpu, info);

    return make_lease(pattern.get(), std::move(image));
}
