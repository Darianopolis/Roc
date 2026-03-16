#include "internal.hpp"

#include "core/stack.hpp"
#include "core/util.hpp"

VkImageUsageFlags gpu_image_usage_to_vk(flags<gpu_image_usage> usage)
{
    VkImageUsageFlags vk_usage = {};
    if (usage.contains(gpu_image_usage::storage))      vk_usage |= VK_IMAGE_USAGE_STORAGE_BIT;
    if (usage.contains(gpu_image_usage::render))       vk_usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (usage.contains(gpu_image_usage::texture))      vk_usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if (usage.contains(gpu_image_usage::transfer_src)) vk_usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (usage.contains(gpu_image_usage::transfer_dst)) vk_usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    return vk_usage;
}

VkFormatFeatureFlags gpu_get_required_format_features(gpu_format format, flags<gpu_image_usage> usage)
{
    VkFormatFeatureFlags features = {};
    if (usage.contains(gpu_image_usage::storage)) features |= VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
    if (usage.contains(gpu_image_usage::render))  features |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT
                                                            |  VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT;
    if (usage.contains(gpu_image_usage::texture)) {
        features |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT
                 |  VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
        if (format->is_ycbcr) features |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_YCBCR_CONVERSION_LINEAR_FILTER_BIT
                                       |  VK_FORMAT_FEATURE_MIDPOINT_CHROMA_SAMPLES_BIT;
    }
    if (usage.contains(gpu_image_usage::transfer_dst)) features |= VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    if (usage.contains(gpu_image_usage::transfer_src)) features |= VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;
    return features;
}

static
VkImageAspectFlagBits gpu_plane_to_aspect(u32 i)
{
    return std::array {
        VK_IMAGE_ASPECT_MEMORY_PLANE_0_BIT_EXT,
        VK_IMAGE_ASPECT_MEMORY_PLANE_1_BIT_EXT,
        VK_IMAGE_ASPECT_MEMORY_PLANE_2_BIT_EXT,
        VK_IMAGE_ASPECT_MEMORY_PLANE_3_BIT_EXT
    }[i];
}

// -----------------------------------------------------------------------------

gpu_image_base::~gpu_image_base()
{
    gpu->image_descriptor_allocator.free(data.id);
}

static
gpu_image_base* get_base(gpu_image* image)
{
    return static_cast<gpu_image_base*>(image->base());
}

auto gpu_image::context()    -> gpu_context*           { return get_base(this)->gpu;           }
auto gpu_image::extent()     -> vec2u32                { return get_base(this)->data.extent;   }
auto gpu_image::format()     -> gpu_format             { return get_base(this)->data.format;   }
auto gpu_image::modifier()   -> gpu_drm_modifier       { return get_base(this)->data.modifier; }
auto gpu_image::view()       -> VkImageView            { return get_base(this)->data.view;     }
auto gpu_image::handle()     -> VkImage                { return get_base(this)->data.image;    }
auto gpu_image::usage()      -> flags<gpu_image_usage> { return get_base(this)->data.usage;    }
auto gpu_image::descriptor() -> gpu_descriptor_id      { return get_base(this)->data.id;       }

// -----------------------------------------------------------------------------

struct gpu_image_vma : gpu_image_base
{
    struct {
        VmaAllocation allocation;
    } vma;

    ~gpu_image_vma();
};

gpu_image_vma::~gpu_image_vma()
{
    gpu->stats.active_images--;

    VmaAllocationInfo alloc_info;
    vmaGetAllocationInfo(gpu->vma, vma.allocation, &alloc_info);
    gpu->stats.active_image_memory -= alloc_info.size;

    gpu->vk.DestroyImageView(gpu->device, view(), nullptr);
    vmaDestroyImage(gpu->vma, handle(), vma.allocation);
}

ref<gpu_image> gpu_image_create(gpu_context* gpu, const gpu_image_create_info& info)
{
    if (info.modifiers) {
        return gpu_image_create_dmabuf(gpu, info);
    }

    auto image = core_create<gpu_image_vma>();
    image->gpu = gpu;

    gpu->stats.active_images++;

    image->data.extent = info.extent;
    image->data.format = info.format;
    image->data.usage = info.usage;

    VmaAllocationInfo alloc_info;
    gpu_check(vmaCreateImage(gpu->vma, ptr_to(VkImageCreateInfo {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = info.format->vk,
        .extent = {info.extent.x, info.extent.y, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = gpu_image_usage_to_vk(info.usage),
        .sharingMode = VK_SHARING_MODE_CONCURRENT,
        .queueFamilyIndexCount = 2,
        .pQueueFamilyIndices = std::array {
            gpu->graphics_queue->family,
            gpu->transfer_queue->family,
        }.data(),
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    }), ptr_to(VmaAllocationCreateInfo {
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
    }), &image->data.image, &image->vma.allocation, &alloc_info));

    gpu->stats.active_image_memory += alloc_info.size;

    gpu_image_init(image.get());

    return image;
}

void gpu_image_init(gpu_image_base* image)
{
    auto* gpu = image->context();

    auto vk_usage = gpu_image_usage_to_vk(image->usage());
    if (vk_usage & (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)) {
        gpu_check(gpu->vk.CreateImageView(gpu->device, ptr_to(VkImageViewCreateInfo {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = image->handle(),
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = image->format()->vk,
            .components {
                .a = image->format()->vk_flags.contains(gpu_vk_format_flag::ignore_alpha)
                    ? VK_COMPONENT_SWIZZLE_ONE
                    : VK_COMPONENT_SWIZZLE_IDENTITY,
            },
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
        }), nullptr, &image->data.view));

        if (vk_usage & (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT)) {
            gpu_allocate_image_descriptor(image);
        }
    }

    auto queue = gpu_get_queue(gpu, gpu_queue_type::transfer);
    auto cmd = gpu_begin(queue);
    gpu_protect(cmd.get(), image);

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
            .image = image->handle(),
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
        }),
    }));

    auto done = gpu_submit(cmd.get(), {});
    gpu_wait(done);
}

void gpu_cmd_copy_image_to_buffer(gpu_commands* cmd, gpu_buffer* buffer, gpu_image* image)
{
    auto* gpu = image->context();
    auto extent = image->extent();

    gpu_protect(cmd, image);
    gpu_protect(cmd, buffer);

    gpu->vk.CmdCopyImageToBuffer(cmd->buffer, image->handle(), VK_IMAGE_LAYOUT_GENERAL, buffer->buffer, 1, ptr_to(VkBufferImageCopy {
        .bufferOffset = 0,
        .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .imageOffset = {},
        .imageExtent = { extent.x, extent.y, 1 },
    }));
}

void gpu_cmd_copy_buffer_to_image(gpu_commands* cmd, gpu_image* image, gpu_buffer* buffer, std::span<const gpu_buffer_image_copy> regions)
{
    auto* gpu = image->context();

    core_thread_stack stack;

    gpu_protect(cmd, image);
    gpu_protect(cmd, buffer);

    auto* copies = stack.allocate<VkBufferImageCopy>(regions.size());
    for (auto[i, region] : regions | std::views::enumerate) {
        copies[i] = {
            .bufferOffset = region.buffer_offset,
            .bufferRowLength = region.buffer_row_length,
            .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
            .imageOffset = { region.image_offset.x, region.image_offset.y },
            .imageExtent = { region.image_extent.x, region.image_extent.y, 1 },
        };
    }

    gpu->vk.CmdCopyBufferToImage(cmd->buffer, buffer->buffer, image->handle(), VK_IMAGE_LAYOUT_GENERAL, regions.size(), copies);
}

void gpu_cmd_copy_memory_to_image(gpu_commands* cmd, gpu_image* image, core_byte_view data, std::span<const gpu_buffer_image_copy> regions)
{
    auto* gpu = image->context();

    // TODO: This should be stored persistently for transfers
    ref buffer = gpu_buffer_create(gpu, data.size, gpu_buffer_flag::host);

    std::memcpy(buffer->host_address, data.data, data.size);

    gpu_cmd_copy_buffer_to_image(cmd, image, buffer.get(), regions);
}

void gpu_copy_memory_to_image(gpu_image* image, core_byte_view data, std::span<const gpu_buffer_image_copy> regions)
{
    auto queue = gpu_get_queue(image->context(), gpu_queue_type::transfer);
    auto commands = gpu_begin(queue);
    gpu_cmd_copy_memory_to_image(commands.get(), image, data, regions);
    auto done = gpu_submit(commands.get(), {});
    gpu_wait(done);
}

auto gpu_image_compute_linear_offset(gpu_format format, vec2u32 pos, u32 row_stride_bytes) -> u32
{
    auto& fmt = format->info;

    u32 block_x = pos.x / fmt.block_extent.width;
    u32 block_y = pos.y / fmt.block_extent.height;

    return block_y * row_stride_bytes
         + block_x * fmt.texel_block_size;
}

// -----------------------------------------------------------------------------

ref<gpu_sampler> gpu_sampler_create(gpu_context* gpu, const gpu_sampler_create_info& info)
{
    ref sampler = core_create<gpu_sampler>();
    sampler->gpu = gpu;

    gpu->stats.active_samplers++;

    gpu_check(gpu->vk.CreateSampler(gpu->device, ptr_to(VkSamplerCreateInfo {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = info.mag,
        .minFilter = info.min,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
        .anisotropyEnable = false,
        .maxLod = VK_LOD_CLAMP_NONE,
        .borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
    }), nullptr, &sampler->sampler));

    gpu_allocate_sampler_descriptor(sampler.get());

    return sampler;
}

gpu_sampler::~gpu_sampler()
{
    gpu->stats.active_samplers--;

    gpu->sampler_descriptor_allocator.free(id);

    gpu->vk.DestroySampler(gpu->device, sampler, nullptr);
}

// -----------------------------------------------------------------------------

u32 gpu_find_vk_memory_type_index(gpu_context* gpu, u32 type_filter, VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties props;
    gpu->vk.GetPhysicalDeviceMemoryProperties(gpu->physical_device, &props);

    for (u32 i = 0; i < props.memoryTypeCount; ++i) {
        if (!(type_filter & (1 << i))) continue;
        if ((props.memoryTypes[i].propertyFlags & properties) != properties) continue;

        return i;
    }

    log_error("Failed to find suitable memory type");
    return ~0u;
}

// -----------------------------------------------------------------------------

struct gpu_image_dmabuf : gpu_image_base
{
    core_fixed_array<VkDeviceMemory, gpu_dma_max_planes> memory;

    struct {
        usz allocation_size;
    } stats;

    ~gpu_image_dmabuf();
};

gpu_image_dmabuf::~gpu_image_dmabuf()
{
    gpu->stats.active_images--;
    gpu->stats.active_image_memory -= stats.allocation_size;

    gpu->vk.DestroyImageView(gpu->device, view(), nullptr);
    gpu->vk.DestroyImage(gpu->device, handle(), nullptr);

    for (auto mem : memory) {
        gpu->vk.FreeMemory(gpu->device, mem, nullptr);
    }
}

ref<gpu_image> gpu_image_create_dmabuf(gpu_context* gpu, const gpu_image_create_info& info)
{
    auto image = core_create<gpu_image_dmabuf>();
    image->gpu = gpu;

    image->data.extent = info.extent;
    image->data.format = info.format;
    image->data.usage = info.usage;

    auto vk_usage = gpu_image_usage_to_vk(info.usage);

    gpu_check(gpu->vk.CreateImage(gpu->device, ptr_to(VkImageCreateInfo {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = gpu_vk_make_chain_in({
            ptr_to(VkExternalMemoryImageCreateInfo {
                .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
                .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
            }),
            ptr_to(VkImageDrmFormatModifierListCreateInfoEXT {
                .sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_LIST_CREATE_INFO_EXT,
                .drmFormatModifierCount = u32(info.modifiers->size()),
                .pDrmFormatModifiers = std::span(*info.modifiers).data(),
            }),
        }),
        .imageType = VK_IMAGE_TYPE_2D,
        .format = info.format->vk,
        .extent = {info.extent.x, info.extent.y, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT,
        .usage = vk_usage,
        .sharingMode = VK_SHARING_MODE_CONCURRENT,
        .queueFamilyIndexCount = 2,
        .pQueueFamilyIndices = std::array {
            gpu->graphics_queue->family,
            gpu->transfer_queue->family,
        }.data(),
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    }), nullptr, &image->data.image));
    core_assert(image->handle());

    // Allocate memory

    VkMemoryRequirements mem_reqs;
    gpu->vk.GetImageMemoryRequirements(gpu->device, image->handle(), &mem_reqs);

    auto index = gpu_find_vk_memory_type_index(gpu, mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    gpu_check(gpu->vk.AllocateMemory(gpu->device, ptr_to(VkMemoryAllocateInfo {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = gpu_vk_make_chain_in({
            ptr_to(VkExportMemoryAllocateInfo {
                .sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO,
                .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
            }),
            ptr_to(VkMemoryDedicatedAllocateInfo {
                .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
                .image = image->handle(),
            })
        }),
        .allocationSize = mem_reqs.size,
        .memoryTypeIndex = index,
    }), nullptr, &image->memory[0]));
    core_assert(image->memory[0]);
    image->memory.count = 1;

    gpu_check(gpu->vk.BindImageMemory(gpu->device, image->handle(), image->memory[0], 0));

    // Stats

    gpu->stats.active_images++;

    image->stats.allocation_size += mem_reqs.size;
    gpu->stats.active_image_memory += mem_reqs.size;

    // Query modifier

    VkImageDrmFormatModifierPropertiesEXT image_drm_format_mod_props {
        .sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_PROPERTIES_EXT,
    };
    gpu->vk.GetImageDrmFormatModifierPropertiesEXT(gpu->device, image->handle(), &image_drm_format_mod_props);
    image->data.modifier = image_drm_format_mod_props.drmFormatModifier;

    // Initialize

    gpu_image_init(image.get());

    return image;
}

gpu_dma_params gpu_image_export(gpu_image* _image)
{
    auto* image = dynamic_cast<gpu_image_dmabuf*>(_image->base());
    core_assert(image);

    auto* gpu = image->gpu;

    gpu_dma_params params = {};

    params.extent = image->extent();
    params.format = image->format();
    params.modifier = image->modifier();

    // Query plane layouts

    auto* mod_props = gpu_get_format_properties(gpu, image->format(), image->usage())->for_mod(params.modifier);
    params.planes.count = mod_props->plane_count;
    for (u32 i = 0; i < mod_props->plane_count; ++i) {
        VkSubresourceLayout layout;
        gpu->vk.GetImageSubresourceLayout(gpu->device, image->handle(),
            ptr_to(VkImageSubresource{gpu_plane_to_aspect(i), 0, 0}),
            &layout);

        params.planes[i].offset = layout.offset;
        params.planes[i].stride = layout.rowPitch;
    }

    // Export file descriptors

    auto export_fd = [&](VkDeviceMemory mem) {
        int _fd = -1;
        gpu_check(gpu->vk.GetMemoryFdKHR(gpu->device, ptr_to(VkMemoryGetFdInfoKHR {
            .sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR,
            .memory = image->memory[0],
            .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
        }), &_fd));
        return core_fd_adopt(_fd);
    };

    if (image->memory.count == 1) {
        auto fd = export_fd(image->memory[0]);
        for (u32 i = 0; i < mod_props->plane_count; ++i) {
            params.planes[i].fd = fd;
        }
    } else {
        core_assert(image->memory.count == mod_props->plane_count);
        for (u32 i = 0; i < mod_props->plane_count; ++i) {
            params.planes[i].fd = export_fd(image->memory[i]);
        }
    }

    return params;
}

ref<gpu_image> gpu_image_import(gpu_context* gpu, const gpu_dma_params& params, flags<gpu_image_usage> usage)
{
    core_assert(!usage.empty());

    auto props = gpu_get_format_properties(gpu, params.format, usage)->for_mod(params.modifier);
    if (!props) {
        log_error("Format {} cannot be used with modifier: {}", params.format->name, gpu_get_modifier_name(params.modifier));
        return nullptr;
    }

    if (params.disjoint && !(props->features & VK_FORMAT_FEATURE_2_DISJOINT_BIT)) {
        log_error("Format {} with modifier {} does not support disjoint images", params.format->name, gpu_get_modifier_name(params.modifier));
        return nullptr;
    }

    auto image = core_create<gpu_image_dmabuf>();
    image->gpu = gpu;

    gpu->stats.active_images++;

    image->data.extent = params.extent;
    image->data.format = params.format;
    image->data.usage = usage;
    image->data.modifier = params.modifier;

    static constexpr auto handle_type = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

    VkSubresourceLayout plane_layouts[gpu_dma_max_planes] = {};
    for (u32 i = 0; i < params.planes.count; ++i) {
        plane_layouts[i].offset = params.planes[i].offset;
        plane_layouts[i].rowPitch = params.planes[i].stride;
    }

    VkImageCreateFlags img_create_flags = {};
    if (params.disjoint) img_create_flags |= VK_IMAGE_CREATE_DISJOINT_BIT;
    gpu_check(gpu->vk.CreateImage(gpu->device, ptr_to(VkImageCreateInfo {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = gpu_vk_make_chain_in({
            ptr_to(VkExternalMemoryImageCreateInfo {
                .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
                .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
            }),
            ptr_to(VkImageDrmFormatModifierExplicitCreateInfoEXT {
                .sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT,
                .drmFormatModifier = params.modifier,
                .drmFormatModifierPlaneCount = u32(params.planes.count),
                .pPlaneLayouts = plane_layouts,
            }),
        }),
        .flags = img_create_flags,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = params.format->vk,
        .extent = {image->extent().x, image->extent().y, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT,
        .usage = gpu_image_usage_to_vk(usage),
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    }), nullptr, &image->data.image));

    auto mem_count = params.disjoint ? params.planes.count : 1;
    image->memory.count = mem_count;

    VkBindImageMemoryInfo bind_info[gpu_dma_max_planes] = {};
    VkBindImagePlaneMemoryInfo plane_info[gpu_dma_max_planes] = {};

    for (u32 i = 0; i < mem_count; ++i) {
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
            .image = image->handle(),
        }), &mem_reqs);

        auto mem = gpu_find_vk_memory_type_index(gpu, mem_reqs.memoryRequirements.memoryTypeBits & fd_props.memoryTypeBits, 0);

        // Take a copy of the file descriptor, this will be owned by the bound vulkan memory
        int vk_fd = core_fd_dup_unsafe(fd.get());

        image->stats.allocation_size   += mem_reqs.memoryRequirements.size;
        gpu->stats.active_image_memory += mem_reqs.memoryRequirements.size;

        if (gpu_check(gpu->vk.AllocateMemory(gpu->device, ptr_to(VkMemoryAllocateInfo {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .pNext = gpu_vk_make_chain_in({
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
                    .image = image->handle(),
                }),
            }),
            .allocationSize = mem_reqs.memoryRequirements.size,
            .memoryTypeIndex = mem,
        }), nullptr, &image->memory[i])) != VK_SUCCESS) {
            log_error("Failed to import memory");
            close(vk_fd);
            return nullptr;
        }

        bind_info[i].sType = VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO;
        bind_info[i].image = image->handle();
        bind_info[i].memory = image->memory[i];

        if (params.disjoint) {
            plane_info[i].sType = VK_STRUCTURE_TYPE_BIND_IMAGE_PLANE_MEMORY_INFO;
            plane_info[i].planeAspect = gpu_plane_to_aspect(i);
            bind_info[i].pNext = &plane_info[i];
        }
    }

    gpu_check(gpu->vk.BindImageMemory2(gpu->device, params.planes.count, bind_info));

    gpu_image_init(image.get());

    return image;
}
