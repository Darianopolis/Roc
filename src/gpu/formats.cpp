#include <gpu/internal.hpp>

#include <core/util.hpp>
#include <core/string.hpp>

auto generate_formats() -> std::vector<GpuFormatInfo>
{
#include "formats.inl"

    std::vector<GpuFormatInfo> formats;

    formats.emplace_back(GpuFormatInfo { .name = "UNDEFINED" });

    for (auto[drm, vk, vk_flags] : drm_to_vk) {
        auto fourcc = drmGetFormatName(drm);
        defer { free(fourcc); };

        auto& info = formats.emplace_back(GpuFormatInfo {
            .name = fourcc,
            .is_ycbcr = vkuFormatRequiresYcbcrConversion(vk),
            .drm = drm,
            .vk = vk,
            .vk_flags = vk_flags,
            .info = vkuGetFormatInfo(vk),
        });

        // Find matching _SRGB VkFormat if present
        if (auto vk_name = std::string_view(string_VkFormat(vk)); vk_name.ends_with("_UNORM")) {
            auto vk_formats = magic_enum::enum_values<VkFormat>();
            auto srgb = std::ranges::find(vk_formats, replace_suffix(vk_name, "_UNORM", "_SRGB"), string_VkFormat);
            if (srgb != vk_formats.end()) info.vk_srgb = *srgb;
        }
    }

    debug_assert(formats.size() < std::numeric_limits<decltype(GpuFormat::index)>::max());

    return formats;
}

static
const std::vector<GpuFormatInfo> gpu_format_infos = generate_formats();

auto gpu_get_format_infos() -> std::span<const GpuFormatInfo>
{
    return gpu_format_infos;
}

// -----------------------------------------------------------------------------

auto gpu_format_from_drm(GpuDrmFormat drm_format) -> GpuFormat
{
    for (auto[i, f] : gpu_format_infos | std::views::enumerate) {
        if (f.drm == drm_format) return GpuFormat(i);
    }
    return {};
}

auto gpu_format_from_vk(VkFormat vk_format, Flags<GpuVulkanFormatFlag> vk_flags) -> GpuFormat
{
    for (auto[i, f] : gpu_format_infos | std::views::enumerate) {
        if (f.vk == vk_format && f.vk_flags == vk_flags) return GpuFormat(i);
    }
    return {};
}

static
auto query_format_support(
    Gpu* gpu,
    VkFormat format, VkFormat srgb_format,
    VkImageUsageFlags usage,
    const VkDrmFormatModifierProperties2EXT* drm_props,
    bool* has_mutable_srgb,
    VkImageFormatProperties* out,
    VkExternalMemoryProperties* out_external_memory_properties) -> bool
{
    bool has_srgb_format = srgb_format != VK_FORMAT_UNDEFINED;

    VkFormat view_formats[2] {
        format,
        srgb_format,
    };

    VkPhysicalDeviceExternalImageFormatInfo external_image_info;
    VkPhysicalDeviceImageDrmFormatModifierInfoEXT mod_info;
    VkExternalImageFormatProperties external_image_props;

    VkImageFormatListCreateInfo format_list {
        .sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO,
        .viewFormatCount = has_srgb_format ? 2u : 1u,
        .pViewFormats = view_formats,
    };

    VkImageCreateFlags image_flags = {};

    VkPhysicalDeviceImageFormatInfo2 format_info {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,
        .pNext = &format_list,
        .format = format,
        .type = VK_IMAGE_TYPE_2D,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = usage,
        .flags = image_flags | (has_srgb_format ? VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT : 0),
    };

    VkImageFormatProperties2 format_props {
        .sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2,
    };

    if (drm_props) {
        format_info.tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
        mod_info = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_DRM_FORMAT_MODIFIER_INFO_EXT,
            .pNext = &format_list,
            .drmFormatModifier = drm_props->drmFormatModifier,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };
        external_image_info = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO,
            .pNext = &mod_info,
            .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
        };
        format_info.pNext = &external_image_info;

        external_image_props = {
            .sType = VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES,
        };
        format_props.pNext = &external_image_props;
    };

    auto check = [&] {
        auto res = gpu_check(gpu->vk.GetPhysicalDeviceImageFormatProperties2(gpu->physical_device, &format_info, &format_props), VK_ERROR_FORMAT_NOT_SUPPORTED);
        if (res != VK_SUCCESS) {
            // Unsupported
            return false;
        }

        *out = format_props.imageFormatProperties;
        if (out_external_memory_properties) {
            *out_external_memory_properties = external_image_props.externalMemoryProperties;
        }

        return true;
    };

    if (check()) {
        *has_mutable_srgb = has_srgb_format;
        return true;
    }

    *has_mutable_srgb = false;

    if (!has_srgb_format) return false;

    // Try without srgb format
    format_list.viewFormatCount = 1;
    format_info.flags = image_flags;
    return check();
}

static
auto load_format_props(Gpu* gpu, GpuFormatProperties& props, GpuFormat format, Flags<GpuImageUsage> usage) -> const GpuFormatProperties*
{
    auto vk_usage = gpu_image_usage_to_vk(usage);
    auto required_features = gpu_get_required_format_features(format, usage);
    auto has_all_features = [&](VkFormatFeatureFlags features) {
        return (features & required_features) == required_features;
    };

    // Query format properties

    VkDrmFormatModifierPropertiesList2EXT mod_list = {
        .sType = VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_2_EXT,
    };
    VkFormatProperties2 vk_props = {
        .sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2,
        .pNext = &mod_list,
    };
    gpu->vk.GetPhysicalDeviceFormatProperties2(gpu->physical_device, format->vk, &vk_props);

    std::vector<VkDrmFormatModifierProperties2EXT> mod_props(mod_list.drmFormatModifierCount);
    mod_list.pDrmFormatModifierProperties = mod_props.data();

    gpu->vk.GetPhysicalDeviceFormatProperties2(gpu->physical_device, format->vk, &vk_props);

    // Query optimal tiling properties

    if (has_all_features(vk_props.formatProperties.optimalTilingFeatures)) {
        VkImageFormatProperties image_props;
        bool has_mutable_srgb = false;
        if (query_format_support(gpu, format->vk, format->vk_srgb, vk_usage, nullptr, &has_mutable_srgb, &image_props, nullptr)) {
            props.opt_props = std::unique_ptr<GpuFormatModifierProperties>(new GpuFormatModifierProperties {
                .modifier = DRM_FORMAT_MOD_INVALID,
                .features = vk_props.formatProperties.optimalTilingFeatures,
                .max_extent = {image_props.maxExtent.width, image_props.maxExtent.height},
                .has_mutable_srgb = has_mutable_srgb,
            });
        }
    }

    // Query drm modifier properties

    for (auto& mod : mod_props) {
        if (!has_all_features(mod.drmFormatModifierTilingFeatures)) continue;

        VkImageFormatProperties image_props;
        bool has_mutable_srgb = false;
        VkExternalMemoryProperties ext_mem_props;
        if (query_format_support(gpu, format->vk, format->vk_srgb, vk_usage, &mod, &has_mutable_srgb, &image_props, &ext_mem_props)) {
            props.mod_props.emplace_back(GpuFormatModifierProperties {
                .modifier = mod.drmFormatModifier,
                .features = mod.drmFormatModifierTilingFeatures,
                .plane_count = mod.drmFormatModifierPlaneCount,
                .ext_mem_props = ext_mem_props,
                .max_extent = {image_props.maxExtent.width, image_props.maxExtent.height},
                .has_mutable_srgb = has_mutable_srgb,
            });
            props.mods.insert(mod.drmFormatModifier);
        }
    }

    return &props;
}

auto gpu_get_format_properties(Gpu* gpu, GpuFormat format, Flags<GpuImageUsage> usage) -> const GpuFormatProperties*
{
    debug_assert(!usage.empty());

    GpuFormatPropertiesKey key { format->vk, gpu_image_usage_to_vk(usage) };

    auto iter = gpu->format_props.find(key);

    return iter == gpu->format_props.end()
        ? load_format_props(gpu, gpu->format_props[key], format, usage)
        : &iter->second;
}

auto gpu_get_modifier_name(GpuDrmModifier mod) -> std::string
{
    auto name = drmGetFormatModifierName(mod);
    std::string str = name ?: "UNKNOWN";
    free(name);
    return str;
}

auto gpu_intersect_format_sets(std::span<const GpuFormatSet* const> sets) -> GpuFormatSet
{
    if (sets.empty()) return {};

    GpuFormatSet out;

    auto first = sets.front();
    auto rest = sets.subspan(1);
    for (auto[format, mods] : *first) {
        std::vector<const GpuFormatModifierSet*> modifier_sets{&mods};
        for (auto& r : rest) modifier_sets.emplace_back(&r->get(format));
        auto modifier_set = gpu_intersect_format_modifiers(modifier_sets);
        if (!modifier_set.empty()) {
            out.entries[format] = std::move(modifier_set);
        }
    }

    return out;
}

auto gpu_intersect_format_modifiers(std::span<const GpuFormatModifierSet* const> sets) -> GpuFormatModifierSet
{
    if (sets.empty()) return {};

    GpuFormatModifierSet out;
    auto first = sets.front();
    auto rest = sets.subspan(1);
    for (auto mod : *first) {
        for (auto set : rest) {
            if (!set->contains(mod)) continue;
        }
        out.insert(mod);
    }
    return out;
}
