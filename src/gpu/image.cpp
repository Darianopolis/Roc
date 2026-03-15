#include "internal.hpp"

#include "core/stack.hpp"
#include "core/util.hpp"

VkImageUsageFlags gpu_image_usage_to_vk(core::Flags<gpu::ImageUsage> usage)
{
    VkImageUsageFlags vk_usage = {};
    if (usage.contains(gpu::ImageUsage::storage))      vk_usage |= VK_IMAGE_USAGE_STORAGE_BIT;
    if (usage.contains(gpu::ImageUsage::render))       vk_usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (usage.contains(gpu::ImageUsage::texture))      vk_usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if (usage.contains(gpu::ImageUsage::transfer_src)) vk_usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (usage.contains(gpu::ImageUsage::transfer_dst)) vk_usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    return vk_usage;
}

VkFormatFeatureFlags gpu_get_required_format_features(gpu::Format format, core::Flags<gpu::ImageUsage> usage)
{
    VkFormatFeatureFlags features = {};
    if (usage.contains(gpu::ImageUsage::storage)) features |= VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
    if (usage.contains(gpu::ImageUsage::render))  features |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT
                                                            |  VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT;
    if (usage.contains(gpu::ImageUsage::texture)) {
        features |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT
                 |  VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
        if (format->is_ycbcr) features |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_YCBCR_CONVERSION_LINEAR_FILTER_BIT
                                       |  VK_FORMAT_FEATURE_MIDPOINT_CHROMA_SAMPLES_BIT;
    }
    if (usage.contains(gpu::ImageUsage::transfer_dst)) features |= VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    if (usage.contains(gpu::ImageUsage::transfer_src)) features |= VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;
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
gpu_image_base* get_base(gpu::Image* image)
{
    return static_cast<gpu_image_base*>(image->base());
}

auto gpu::Image::context()    -> gpu::Context*           { return get_base(this)->gpu;           }
auto gpu::Image::extent()     -> vec2u32                { return get_base(this)->data.extent;   }
auto gpu::Image::format()     -> gpu::Format             { return get_base(this)->data.format;   }
auto gpu::Image::modifier()   -> gpu::DrmModifier       { return get_base(this)->data.modifier; }
auto gpu::Image::view()       -> VkImageView            { return get_base(this)->data.view;     }
auto gpu::Image::handle()     -> VkImage                { return get_base(this)->data.image;    }
auto gpu::Image::usage()      -> core::Flags<gpu::ImageUsage> { return get_base(this)->data.usage;    }
auto gpu::Image::descriptor() -> gpu::DescriptorId      { return get_base(this)->data.id;       }

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

core::Ref<gpu::Image> gpu::image::create(gpu::Context* gpu, const gpu::ImageCreateInfo& info)
{
    if (info.modifiers) {
        return gpu_image_create_dmabuf(gpu, info);
    }

    auto image = core::create<gpu_image_vma>();
    image->gpu = gpu;

    gpu->stats.active_images++;

    image->data.extent = info.extent;
    image->data.format = info.format;
    image->data.usage = info.usage;

    VmaAllocationInfo alloc_info;
    gpu_check(vmaCreateImage(gpu->vma, core::ptr_to(VkImageCreateInfo {
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
    }), core::ptr_to(VmaAllocationCreateInfo {
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
        gpu_check(gpu->vk.CreateImageView(gpu->device, core::ptr_to(VkImageViewCreateInfo {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = image->handle(),
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = image->format()->vk,
            .components {
                .a = image->format()->vk_flags.contains(gpu::vk::FormatFlag::ignore_alpha)
                    ? VK_COMPONENT_SWIZZLE_ONE
                    : VK_COMPONENT_SWIZZLE_IDENTITY,
            },
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
        }), nullptr, &image->data.view));

        if (vk_usage & (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT)) {
            gpu_allocate_image_descriptor(image);
        }
    }

    auto queue = gpu::queue::get(gpu, gpu::QueueType::transfer);
    auto cmd = gpu::commands::begin(queue);
    gpu::commands::protect(cmd.get(), image);

    gpu->vk.CmdPipelineBarrier2(cmd->buffer, core::ptr_to(VkDependencyInfo {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = core::ptr_to(VkImageMemoryBarrier2 {
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

    auto done = gpu::submit(cmd.get(), {});
    gpu::wait(done);
}

void gpu::commands::copy_image_to_buffer(gpu::Commands* cmd, gpu::Buffer* buffer, gpu::Image* image)
{
    auto* gpu = image->context();
    auto extent = image->extent();

    gpu::commands::protect(cmd, image);
    gpu::commands::protect(cmd, buffer);

    gpu->vk.CmdCopyImageToBuffer(cmd->buffer, image->handle(), VK_IMAGE_LAYOUT_GENERAL, buffer->buffer, 1, core::ptr_to(VkBufferImageCopy {
        .bufferOffset = 0,
        .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .imageOffset = {},
        .imageExtent = { extent.x, extent.y, 1 },
    }));
}

void gpu::commands::copy_buffer_to_image(gpu::Commands* cmd, gpu::Image* image, gpu::Buffer* buffer, std::span<const gpu::BufferImageCopy> regions)
{
    auto* gpu = image->context();

    core::ThreadStack stack;

    gpu::commands::protect(cmd, image);
    gpu::commands::protect(cmd, buffer);

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

void gpu::commands::copy_memory_to_image(gpu::Commands* cmd, gpu::Image* image, core::ByteView data, std::span<const gpu::BufferImageCopy> regions)
{
    auto* gpu = image->context();

    // TODO: This should be stored persistently for transfers
    core::Ref buffer = gpu::buffer::create(gpu, data.size, gpu::BufferFlag::host);

    std::memcpy(buffer->host_address, data.data, data.size);

    gpu::commands::copy_buffer_to_image(cmd, image, buffer.get(), regions);
}

void gpu::copy_memory_to_image(gpu::Image* image, core::ByteView data, std::span<const gpu::BufferImageCopy> regions)
{
    auto queue = gpu::queue::get(image->context(), gpu::QueueType::transfer);
    auto commands = gpu::commands::begin(queue);
    gpu::commands::copy_memory_to_image(commands.get(), image, data, regions);
    auto done = gpu::submit(commands.get(), {});
    gpu::wait(done);
}

auto gpu::image::compute_linear_offset(gpu::Format format, vec2u32 pos, u32 row_stride_bytes) -> u32
{
    auto& fmt = format->info;

    u32 block_x = pos.x / fmt.block_extent.width;
    u32 block_y = pos.y / fmt.block_extent.height;

    return block_y * row_stride_bytes
         + block_x * fmt.texel_block_size;
}

// -----------------------------------------------------------------------------

core::Ref<gpu::Sampler> gpu::sampler::create(gpu::Context* gpu, const gpu::SamplerCreateInfo& info)
{
    core::Ref sampler = core::create<gpu::Sampler>();
    sampler->gpu = gpu;

    gpu->stats.active_samplers++;

    gpu_check(gpu->vk.CreateSampler(gpu->device, core::ptr_to(VkSamplerCreateInfo {
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

gpu::Sampler::~Sampler()
{
    gpu->stats.active_samplers--;

    gpu->sampler_descriptor_allocator.free(id);

    gpu->vk.DestroySampler(gpu->device, sampler, nullptr);
}

// -----------------------------------------------------------------------------

u32 gpu_find_vk_memory_type_index(gpu::Context* gpu, u32 type_filter, VkMemoryPropertyFlags properties)
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
    core::FixedArray<VkDeviceMemory, gpu::max_dma_planes> memory;

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

core::Ref<gpu::Image> gpu_image_create_dmabuf(gpu::Context* gpu, const gpu::ImageCreateInfo& info)
{
    auto image = core::create<gpu_image_dmabuf>();
    image->gpu = gpu;

    image->data.extent = info.extent;
    image->data.format = info.format;
    image->data.usage = info.usage;

    auto vk_usage = gpu_image_usage_to_vk(info.usage);

    gpu_check(gpu->vk.CreateImage(gpu->device, core::ptr_to(VkImageCreateInfo {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = gpu_vk_make_chain_in({
            core::ptr_to(VkExternalMemoryImageCreateInfo {
                .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
                .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
            }),
            core::ptr_to(VkImageDrmFormatModifierListCreateInfoEXT {
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
    gpu_check(gpu->vk.AllocateMemory(gpu->device, core::ptr_to(VkMemoryAllocateInfo {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = gpu_vk_make_chain_in({
            core::ptr_to(VkExportMemoryAllocateInfo {
                .sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO,
                .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
            }),
            core::ptr_to(VkMemoryDedicatedAllocateInfo {
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

gpu::DmaParams gpu::image::export_dmabuf(gpu::Image* _image)
{
    auto* image = dynamic_cast<gpu_image_dmabuf*>(_image->base());
    core_assert(image);

    auto* gpu = image->gpu;

    gpu::DmaParams params = {};

    params.extent = image->extent();
    params.format = image->format();
    params.modifier = image->modifier();

    // Query plane layouts

    auto* mod_props = gpu::get_format_props(gpu, image->format(), image->usage())->for_mod(params.modifier);
    params.planes.count = mod_props->plane_count;
    for (u32 i = 0; i < mod_props->plane_count; ++i) {
        VkSubresourceLayout layout;
        gpu->vk.GetImageSubresourceLayout(gpu->device, image->handle(),
            core::ptr_to(VkImageSubresource{gpu_plane_to_aspect(i), 0, 0}),
            &layout);

        params.planes[i].offset = layout.offset;
        params.planes[i].stride = layout.rowPitch;
    }

    // Export file descriptors

    auto export_fd = [&](VkDeviceMemory mem) {
        int _fd = -1;
        gpu_check(gpu->vk.GetMemoryFdKHR(gpu->device, core::ptr_to(VkMemoryGetFdInfoKHR {
            .sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR,
            .memory = image->memory[0],
            .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
        }), &_fd));
        return core::fd::adopt(_fd);
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

core::Ref<gpu::Image> gpu::image::import_dmabuf(gpu::Context* gpu, const gpu::DmaParams& params, core::Flags<gpu::ImageUsage> usage)
{
    core_assert(!usage.empty());

    auto props = gpu::get_format_props(gpu, params.format, usage)->for_mod(params.modifier);
    if (!props) {
        log_error("Format {} cannot be used with modifier: {}", params.format->name, gpu::drm_modifier_get_name(params.modifier));
        return nullptr;
    }

    if (params.disjoint && !(props->features & VK_FORMAT_FEATURE_2_DISJOINT_BIT)) {
        log_error("Format {} with modifier {} does not support disjoint images", params.format->name, gpu::drm_modifier_get_name(params.modifier));
        return nullptr;
    }

    auto image = core::create<gpu_image_dmabuf>();
    image->gpu = gpu;

    gpu->stats.active_images++;

    image->data.extent = params.extent;
    image->data.format = params.format;
    image->data.usage = usage;
    image->data.modifier = params.modifier;

    static constexpr auto handle_type = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

    VkSubresourceLayout plane_layouts[gpu::max_dma_planes] = {};
    for (u32 i = 0; i < params.planes.count; ++i) {
        plane_layouts[i].offset = params.planes[i].offset;
        plane_layouts[i].rowPitch = params.planes[i].stride;
    }

    VkImageCreateFlags img_create_flags = {};
    if (params.disjoint) img_create_flags |= VK_IMAGE_CREATE_DISJOINT_BIT;
    gpu_check(gpu->vk.CreateImage(gpu->device, core::ptr_to(VkImageCreateInfo {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = gpu_vk_make_chain_in({
            core::ptr_to(VkExternalMemoryImageCreateInfo {
                .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
                .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
            }),
            core::ptr_to(VkImageDrmFormatModifierExplicitCreateInfoEXT {
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

    VkBindImageMemoryInfo bind_info[gpu::max_dma_planes] = {};
    VkBindImagePlaneMemoryInfo plane_info[gpu::max_dma_planes] = {};

    for (u32 i = 0; i < mem_count; ++i) {
        auto fd = params.planes[i].fd;
        VkMemoryFdPropertiesKHR fd_props = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR,
        };
        gpu_check(gpu->vk.GetMemoryFdPropertiesKHR(gpu->device, handle_type, fd.get(), &fd_props));

        VkMemoryRequirements2 mem_reqs = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,
        };
        gpu->vk.GetImageMemoryRequirements2(gpu->device, core::ptr_to(VkImageMemoryRequirementsInfo2 {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2,
            .pNext = params.disjoint
                ? core::ptr_to(VkImagePlaneMemoryRequirementsInfo {
                    .sType = VK_STRUCTURE_TYPE_IMAGE_PLANE_MEMORY_REQUIREMENTS_INFO,
                    .planeAspect = gpu_plane_to_aspect(i),
                })
                : nullptr,
            .image = image->handle(),
        }), &mem_reqs);

        auto mem = gpu_find_vk_memory_type_index(gpu, mem_reqs.memoryRequirements.memoryTypeBits & fd_props.memoryTypeBits, 0);

        // Take a copy of the file descriptor, this will be owned by the bound vulkan memory
        int vk_fd = core::fd::dup_unsafe(fd.get());

        image->stats.allocation_size   += mem_reqs.memoryRequirements.size;
        gpu->stats.active_image_memory += mem_reqs.memoryRequirements.size;

        if (gpu_check(gpu->vk.AllocateMemory(gpu->device, core::ptr_to(VkMemoryAllocateInfo {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .pNext = gpu_vk_make_chain_in({
                core::ptr_to(VkImportMemoryFdInfoKHR {
                    .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR,
                    .handleType = handle_type,
                    .fd = vk_fd,
                }),
                core::ptr_to(VkExportMemoryAllocateInfo {
                    .sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO,
                    .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
                }),
                core::ptr_to(VkMemoryDedicatedAllocateInfo {
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
