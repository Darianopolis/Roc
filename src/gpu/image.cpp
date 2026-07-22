#include "internal.hpp"

#include <core/stack.hpp>
#include <core/util.hpp>

auto gpu_image_usage_to_vulkan(Flags<GpuImageUsage> usage) -> VkImageUsageFlags
{
    VkImageUsageFlags vk_usage = {};
    if (usage.contains(GpuImageUsage::storage))      vk_usage |= VK_IMAGE_USAGE_STORAGE_BIT;
    if (usage.contains(GpuImageUsage::sampled))      vk_usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if (usage.contains(GpuImageUsage::transfer_src)) vk_usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (usage.contains(GpuImageUsage::transfer_dst)) vk_usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    return vk_usage;
}

auto gpu_get_required_format_features(GpuFormat format, Flags<GpuImageUsage> usage) -> VkFormatFeatureFlags2
{
    VkFormatFeatureFlags features = {};
    if (usage.contains(GpuImageUsage::storage)) features |= VK_FORMAT_FEATURE_2_STORAGE_IMAGE_BIT;
    if (usage.contains(GpuImageUsage::sampled)) {
        features |= VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT
                 |  VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
        if (format->is_ycbcr) features |= VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_YCBCR_CONVERSION_LINEAR_FILTER_BIT
                                       |  VK_FORMAT_FEATURE_2_MIDPOINT_CHROMA_SAMPLES_BIT;
    }
    if (usage.contains(GpuImageUsage::transfer_dst)) features |= VK_FORMAT_FEATURE_2_TRANSFER_DST_BIT;
    if (usage.contains(GpuImageUsage::transfer_src)) features |= VK_FORMAT_FEATURE_2_TRANSFER_SRC_BIT;
    return features;
}

static
auto gpu_plane_to_aspect(u32 i) -> VkImageAspectFlagBits
{
    return std::array {
        VK_IMAGE_ASPECT_MEMORY_PLANE_0_BIT_EXT,
        VK_IMAGE_ASPECT_MEMORY_PLANE_1_BIT_EXT,
        VK_IMAGE_ASPECT_MEMORY_PLANE_2_BIT_EXT,
        VK_IMAGE_ASPECT_MEMORY_PLANE_3_BIT_EXT
    }[i];
}

auto gpu_find_memory_type_index(Gpu* gpu, u32 type_filter, VkMemoryPropertyFlags required, VkMemoryPropertyFlags disallowed) -> std::optional<u32>
{
    VkPhysicalDeviceMemoryProperties props;
    gpu->vk.GetPhysicalDeviceMemoryProperties(gpu->physical_device, &props);

    for (u32 i = 0; i < props.memoryTypeCount; ++i) {
        if (!(type_filter & (1 << i))) continue;
        if ((props.memoryTypes[i].propertyFlags & required) != required) continue;
        if (props.memoryTypes[i].propertyFlags & disallowed) continue;

        return i;
    }

    log_error("Failed to find suitable memory type");
    return std::nullopt;
}

// -----------------------------------------------------------------------------
//      Structures
// -----------------------------------------------------------------------------

GpuImageBase::~GpuImageBase()
{
    gpu->image_descriptor_allocator.free(sampled_id);
    gpu->image_descriptor_allocator.free(srgb_id);
    gpu->image_descriptor_allocator.free(storage_id);

    gpu->vk.DestroyImageView(gpu->device, view, nullptr);
    gpu->vk.DestroyImageView(gpu->device, srgb_view, nullptr);

    gpu->stats.active_images--;

    gpu->vk.DestroyImage(gpu->device, image, nullptr);

    for (auto mem : memory) {
        gpu->vk.FreeMemory(gpu->device, mem, nullptr);
    }
}

// -----------------------------------------------------------------------------
//      Initialization
// -----------------------------------------------------------------------------

static
auto create_image_view(GpuImageBase* image, bool srgb) -> VkImageView
{
    auto* gpu = image->gpu;

    auto vk_usage = gpu_image_usage_to_vulkan(image->usage);
    if (srgb) vk_usage &= ~VkImageUsageFlags(VK_IMAGE_USAGE_STORAGE_BIT);

    VkImageView view;
    gpu_check(gpu->vk.CreateImageView(gpu->device, ptr_to(VkImageViewCreateInfo {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext = ptr_to(VkImageViewUsageCreateInfo {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO,
            .usage = vk_usage,
        }),
        .image = image->image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = srgb ? image->format->vk_srgb : image->format->vk,
        .components {
            .a = image->format->vk_flags.contains(GpuVulkanFormatFlag::ignore_alpha)
                ? VK_COMPONENT_SWIZZLE_ONE
                : VK_COMPONENT_SWIZZLE_IDENTITY,
        },
        .subresourceRange = { image->format->aspect, 0, 1, 0, 1 },
    }), nullptr, &view));

    return view;
}

static
void image_init(GpuImageBase* image, const GpuFormatModifierProperties* props)
{
    auto* gpu = image->gpu;

    auto vk_usage = gpu_image_usage_to_vulkan(image->usage);
    if (vk_usage & (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)) {
        image->view = create_image_view(image, false);

        if (vk_usage & VK_IMAGE_USAGE_SAMPLED_BIT) {
            image->sampled_id = gpu_allocate_image_descriptor(gpu, image->view, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
        }

        if (props->has_mutable_srgb && (vk_usage & VK_IMAGE_USAGE_SAMPLED_BIT)) {
            image->srgb_view = create_image_view(image, true);
            image->srgb_id = gpu_allocate_image_descriptor(gpu, image->srgb_view, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
        }

        if (vk_usage & VK_IMAGE_USAGE_STORAGE_BIT) {
            image->storage_id = gpu_allocate_image_descriptor(gpu, image->view, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
        }
    }

    auto cmd = gpu_record(gpu);
    gpu_protect(cmd, image);

    gpu->vk.CmdPipelineBarrier2(cmd->buffer, ptr_to(VkDependencyInfo {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = ptr_to(VkImageMemoryBarrier2 {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            .dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image->image,
            .subresourceRange = { image->format->aspect, 0, 1, 0, 1 },
        }),
    }));
}

static
void image_create(
    GpuImageBase* image,
    const GpuFormatModifierProperties* props,
    VkImageCreateFlags flags,
    VkImageTiling tiling,
    const void* p_next = nullptr)
{
    VkImageCreateInfo               image_ci   = {};
    VkExternalMemoryImageCreateInfo ext_mem_ci = {};
    VkImageFormatListCreateInfo     formats_ci = {};
    std::array<VkFormat, 2>         formats    = {};

    image_ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_ci.pNext = p_next;
    image_ci.flags = flags;
    image_ci.imageType = VK_IMAGE_TYPE_2D;
    image_ci.format = image->format->vk;
    image_ci.extent = {image->extent.x, image->extent.y, 1};
    image_ci.mipLevels = 1;
    image_ci.arrayLayers = 1;
    image_ci.samples = VK_SAMPLE_COUNT_1_BIT;
    image_ci.tiling = tiling;
    image_ci.usage = gpu_image_usage_to_vulkan(image->usage);
    image_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (props->has_mutable_srgb) {
        formats_ci.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO;
        formats_ci.viewFormatCount = 2;
        formats_ci.pViewFormats = formats.data();
        formats[0] = image->format->vk;
        formats[1] = image->format->vk_srgb;
        formats_ci.pNext = image_ci.pNext;
        image_ci.pNext = &formats_ci;
        image_ci.flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
        image_ci.flags |= VK_IMAGE_CREATE_EXTENDED_USAGE_BIT;
    }

    if (tiling == VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT) {
        ext_mem_ci.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
        ext_mem_ci.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
        ext_mem_ci.pNext = image_ci.pNext;
        image_ci.pNext = &ext_mem_ci;
    }

    gpu_check(image->gpu->vk.CreateImage(image->gpu->device, &image_ci, nullptr, &image->image));
}

// -----------------------------------------------------------------------------
//      Creation
// -----------------------------------------------------------------------------

auto gpu_image_create(Gpu* gpu, const GpuImageCreateInfo& info) -> Ref<GpuImage>
{
    bool is_dmabuf = info.modifiers;

    auto image = ref_create<GpuImageBase>();
    image->gpu = gpu;

    gpu->stats.active_images++;

    image->extent = info.extent;
    image->format = info.format;
    image->usage  = info.usage;

    auto props = gpu_get_format_properties(gpu, image->format, image->usage)->opt_props.get();

    if (is_dmabuf) {
        image_create(image.get(), props, {},
            VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT,
            ptr_to(VkImageDrmFormatModifierListCreateInfoEXT {
                .sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_LIST_CREATE_INFO_EXT,
                .drmFormatModifierCount = num_cast<u32>(info.modifiers->size()),
                .pDrmFormatModifiers = std::span(*info.modifiers).data(),
            }));
    } else {
        image_create(image.get(), props, {}, VK_IMAGE_TILING_OPTIMAL);
    }

    // Allocate memory

    VkMemoryRequirements mem_reqs;
    gpu->vk.GetImageMemoryRequirements(gpu->device, image->image, &mem_reqs);

    auto index = info.flags.contains(GpuImageFlag::host)
        ? gpu_find_memory_type_index(gpu, mem_reqs.memoryTypeBits, 0, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
        : gpu_find_memory_type_index(gpu, mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    gpu_check(gpu->vk.AllocateMemory(gpu->device, ptr_to(VkMemoryAllocateInfo {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = is_dmabuf
            ? gpu_vulkan_make_chain({{
                ptr_to(VkExportMemoryAllocateInfo {
                    .sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO,
                    .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
                }),
                ptr_to(VkMemoryDedicatedAllocateInfo {
                    .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
                    .image = image->image,
                })
            }})
            : nullptr,
        .allocationSize = mem_reqs.size,
        .memoryTypeIndex = index.value(),
    }), nullptr, &image->memory[0]));

    gpu_check(gpu->vk.BindImageMemory2(gpu->device, 1, ptr_to(VkBindImageMemoryInfo {
        .sType = VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO,
        .image = image->image,
        .memory = image->memory[0],
    })));

    image->memory.count = 1;

    if (is_dmabuf) {
        // Query modifier

        VkImageDrmFormatModifierPropertiesEXT image_drm_format_mod_props {
            .sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_PROPERTIES_EXT,
        };
        gpu->vk.GetImageDrmFormatModifierPropertiesEXT(gpu->device, image->image, &image_drm_format_mod_props);
        image->modifier = image_drm_format_mod_props.drmFormatModifier;
    }

    image_init(image.get(), props);

    return image;
}

// -----------------------------------------------------------------------------
//      Import
// -----------------------------------------------------------------------------

auto gpu_image_import(Gpu* gpu, const GpuDmaParams& params, Flags<GpuImageUsage> usage) -> Ref<GpuImage>
{
    debug_assert(!usage.empty());

    auto props = gpu_get_format_properties(gpu, params.format, usage)->for_mod(params.modifier);
    if (!props) {
        log_error("Format {} cannot be used with modifier: {}", params.format->name, gpu_get_modifier_name(params.modifier));
        return nullptr;
    }

    if (params.disjoint && !(props->features & VK_FORMAT_FEATURE_2_DISJOINT_BIT)) {
        log_error("Format {} with modifier {} does not support disjoint images", params.format->name, gpu_get_modifier_name(params.modifier));
        return nullptr;
    }

    auto image = ref_create<GpuImageBase>();
    image->gpu = gpu;

    gpu->stats.active_images++;

    image->extent = params.extent;
    image->format = params.format;
    image->usage = usage;
    image->modifier = params.modifier;

    static constexpr auto handle_type = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

    VkSubresourceLayout plane_layouts[gpu_dma_max_planes] = {};
    for (u32 i = 0; i < params.planes.count; ++i) {
        plane_layouts[i].offset = params.planes[i].offset;
        plane_layouts[i].rowPitch = params.planes[i].stride;
    }

    image_create(image.get(), props,
        params.disjoint ? VK_IMAGE_CREATE_DISJOINT_BIT : 0,
        VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT,
        ptr_to(VkImageDrmFormatModifierExplicitCreateInfoEXT {
            .sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT,
            .drmFormatModifier = params.modifier,
            .drmFormatModifierPlaneCount = num_cast<u32>(params.planes.count),
            .pPlaneLayouts = plane_layouts,
        }));

    image->memory.count = params.disjoint ? params.planes.count : 1;

    VkBindImageMemoryInfo      bind_info [gpu_dma_max_planes] = {};
    VkBindImagePlaneMemoryInfo plane_info[gpu_dma_max_planes] = {};

    for (u32 i = 0; i < image->memory.count; ++i) {
        auto fd = params.planes[i].fd;
        VkMemoryFdPropertiesKHR fd_props = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR,
        };
        gpu_check(gpu->vk.GetMemoryFdPropertiesKHR(gpu->device, handle_type, fd.get(), &fd_props));

        VkMemoryRequirements2 mem_reqs = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,
        };
        gpu->vk.GetImageMemoryRequirements2(gpu->device, ptr_to(VkImageMemoryRequirementsInfo2 {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2,
            .pNext = params.disjoint
                ? ptr_to(VkImagePlaneMemoryRequirementsInfo {
                    .sType = VK_STRUCTURE_TYPE_IMAGE_PLANE_MEMORY_REQUIREMENTS_INFO,
                    .planeAspect = gpu_plane_to_aspect(i),
                })
                : nullptr,
            .image = image->image,
        }), &mem_reqs);

        auto mem = gpu_find_memory_type_index(gpu, mem_reqs.memoryRequirements.memoryTypeBits & fd_props.memoryTypeBits, 0);

        // Take a copy of the file descriptor, ownership is transfered to Vulkan on successfull import
        fd_t vk_fd = fd_dup_unsafe(fd.get());
        defer { if (fd_is_valid(vk_fd)) close(vk_fd); };

        gpu_check(gpu->vk.AllocateMemory(gpu->device, ptr_to(VkMemoryAllocateInfo {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .pNext = gpu_vulkan_make_chain({{
                ptr_to(VkImportMemoryFdInfoKHR {
                    .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR,
                    .handleType = handle_type,
                    .fd = vk_fd,
                }),
                ptr_to(VkExportMemoryAllocateInfo {
                    .sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO,
                    .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
                }),
                ptr_to(VkMemoryDedicatedAllocateInfo {
                    .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
                    .image = image->image,
                }),
            }}),
            .allocationSize = mem_reqs.memoryRequirements.size,
            .memoryTypeIndex = mem.value(),
        }), nullptr, &image->memory[i]));
        vk_fd = -1;

        bind_info[i].sType = VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO;
        bind_info[i].image = image->image;
        bind_info[i].memory = image->memory[i];

        if (params.disjoint) {
            plane_info[i].sType = VK_STRUCTURE_TYPE_BIND_IMAGE_PLANE_MEMORY_INFO;
            plane_info[i].planeAspect = gpu_plane_to_aspect(i);
            bind_info[i].pNext = &plane_info[i];
        }
    }

    gpu_check(gpu->vk.BindImageMemory2(gpu->device, params.planes.count, bind_info));

    image_init(image.get(), props);

    return image;
}

// -----------------------------------------------------------------------------
//      Export
// -----------------------------------------------------------------------------

auto gpu_image_is_exportable(GpuImage* image) -> bool
{
    return image->base()->modifier != DRM_FORMAT_MOD_INVALID;
}

auto gpu_image_export(GpuImage* _image) -> GpuDmaParams
{
    auto* image = _image->base();
    debug_assert(image);

    auto* gpu = image->gpu;

    GpuDmaParams params = {};

    params.extent = image->extent;
    params.format = image->format;
    params.modifier = image->modifier;

    // Query plane layouts

    auto* mod_props = gpu_get_format_properties(gpu, image->format, image->usage)->for_mod(params.modifier);
    params.planes.count = mod_props->plane_count;
    for (u32 i = 0; i < mod_props->plane_count; ++i) {
        VkSubresourceLayout layout;
        gpu->vk.GetImageSubresourceLayout(gpu->device, image->image,
            ptr_to(VkImageSubresource{gpu_plane_to_aspect(i), 0, 0}),
            &layout);

        params.planes[i].offset = num_cast<u32>(layout.offset);
        params.planes[i].stride = num_cast<u32>(layout.rowPitch);
    }

    // Export file descriptors

    auto export_fd = [&](VkDeviceMemory mem) {
        fd_t _fd = -1;
        gpu_check(gpu->vk.GetMemoryFdKHR(gpu->device, ptr_to(VkMemoryGetFdInfoKHR {
            .sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR,
            .memory = image->memory[0],
            .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
        }), &_fd));
        return Fd(_fd);
    };

    if (image->memory.count == 1) {
        auto fd = export_fd(image->memory[0]);
        for (u32 i = 0; i < mod_props->plane_count; ++i) {
            params.planes[i].fd = fd;
        }
    } else {
        debug_assert(image->memory.count == mod_props->plane_count);
        for (u32 i = 0; i < mod_props->plane_count; ++i) {
            params.planes[i].fd = export_fd(image->memory[i]);
        }
    }

    return params;
}

// -----------------------------------------------------------------------------
//      Transfers
// -----------------------------------------------------------------------------

void gpu_copy_image_to_buffer(GpuBuffer* buffer, GpuImage* _image)
{
    auto* image = _image->base();
    auto* gpu = image->gpu;
    auto extent = image->extent;

    auto cmd = gpu_record(gpu);
    gpu_barrier(cmd, {{_image}}, {{buffer}});

    gpu->vk.CmdCopyImageToBuffer(cmd->buffer, image->image, VK_IMAGE_LAYOUT_GENERAL, buffer->buffer, 1, ptr_to(VkBufferImageCopy {
        .bufferOffset = 0,
        .imageSubresource = { image->format->aspect, 0, 0, 1 },
        .imageOffset = {},
        .imageExtent = { extent.x, extent.y, 1 },
    }));
}

void gpu_copy_buffer_to_image(GpuImage* _image, GpuBuffer* buffer, std::span<const GpuBufferImageCopy> regions)
{
    auto* image = _image->base();
    auto* gpu = image->gpu;

    ThreadStack stack;

    auto* copies = stack.allocate<VkBufferImageCopy>(regions.size());
    for (auto[i, region] : regions | std::views::enumerate) {
        copies[i] = {
            .bufferOffset = region.buffer_offset,
            .bufferRowLength = region.buffer_row_length,
            .imageSubresource = { image->format->aspect, 0, 0, 1 },
            .imageOffset = { region.image_offset.x, region.image_offset.y },
            .imageExtent = { region.image_extent.x, region.image_extent.y, 1 },
        };
    }

    auto cmd = gpu_record(gpu);
    gpu_barrier(cmd, {{buffer}}, {{_image}});

    gpu->vk.CmdCopyBufferToImage(cmd->buffer, buffer->buffer, image->image, VK_IMAGE_LAYOUT_GENERAL, num_cast<u32>(regions.size()), copies);
}

void gpu_copy_memory_to_image(GpuImage* image, std::span<const byte> data, std::span<const GpuBufferImageCopy> regions)
{
    auto* gpu = image->base()->gpu;

    // TODO: This should be stored persistently for transfers
    Ref buffer = gpu_buffer_create(gpu, data.size(), GpuBufferFlag::host);

    std::memcpy(buffer->host_address, data.data(), data.size());

    gpu_copy_buffer_to_image(image, buffer.get(), regions);
}

auto gpu_image_compute_packed_stride(GpuFormat format, u32 width) -> u32
{
    auto bw = format->block_extent.width;
    auto blocks = (width + bw - 1) / bw;
    return blocks * format->texel_block_size;
}

auto gpu_image_compute_linear_offset(GpuFormat format, vec2u32 pos, u32 row_stride_bytes) -> u32
{
    u32 block_x = pos.x / format->block_extent.width;
    u32 block_y = pos.y / format->block_extent.height;

    return block_y * row_stride_bytes
         + block_x * format->texel_block_size;
}
