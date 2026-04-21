#include "internal.hpp"

void gpu_init_descriptors(Gpu* gpu)
{
    auto& vk = gpu->vk;

    VkDescriptorBindingFlags binding_flags = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT
        | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;

    constexpr auto num_image_descriptors_each = 65535;
    constexpr auto num_sampler_descriptors    =    16;

    gpu->image_descriptor_allocator   = GpuDescriptorIdAllocator(num_image_descriptors_each);
    gpu->sampler_descriptor_allocator = GpuDescriptorIdAllocator(num_sampler_descriptors);

    gpu_check(vk.CreateDescriptorSetLayout(gpu->device, ptr_to(VkDescriptorSetLayoutCreateInfo {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext = ptr_to(VkDescriptorSetLayoutBindingFlagsCreateInfo {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT,
            .bindingCount = 3,
            .pBindingFlags = std::array { binding_flags, binding_flags, binding_flags }.data(),
        }),
        .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
        .bindingCount = 3,
        .pBindings = std::array {
            VkDescriptorSetLayoutBinding {
                .binding = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                .descriptorCount = num_image_descriptors_each,
                .stageFlags = VK_SHADER_STAGE_ALL,
            },
            VkDescriptorSetLayoutBinding {
                .binding = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                .descriptorCount = num_image_descriptors_each,
                .stageFlags = VK_SHADER_STAGE_ALL,
            },
            VkDescriptorSetLayoutBinding {
                .binding = 2,
                .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
                .descriptorCount = num_sampler_descriptors,
                .stageFlags = VK_SHADER_STAGE_ALL,
            }
        }.data(),
    }), nullptr, &gpu->set_layout));

    gpu_check(vk.CreatePipelineLayout(gpu->device, ptr_to(VkPipelineLayoutCreateInfo {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &gpu->set_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = ptr_to(VkPushConstantRange {
            .stageFlags = VK_SHADER_STAGE_ALL,
            .size = gpu_push_constant_size,
        }),
    }), nullptr, &gpu->pipeline_layout));

    gpu_check(vk.CreateDescriptorPool(gpu->device, ptr_to(VkDescriptorPoolCreateInfo {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
        .maxSets = 1,
        .poolSizeCount = 3,
        .pPoolSizes = std::array {
            VkDescriptorPoolSize { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, num_image_descriptors_each },
            VkDescriptorPoolSize { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, num_image_descriptors_each },
            VkDescriptorPoolSize { VK_DESCRIPTOR_TYPE_SAMPLER, num_sampler_descriptors },
        }.data(),
    }), nullptr, &gpu->pool));

    gpu_check(vk.AllocateDescriptorSets(gpu->device, ptr_to(VkDescriptorSetAllocateInfo {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = gpu->pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &gpu->set_layout,
    }), &gpu->set));
}

// -----------------------------------------------------------------------------

GpuDescriptorIdAllocator::GpuDescriptorIdAllocator(u32 count)
    : last_id(0)
    , max_id(count)
{
    debug_assert(count <= std::numeric_limits<GpuDescriptorId::underlying_type>::max());
}

auto GpuDescriptorIdAllocator::allocate() -> GpuDescriptorId
{
    if (!freelist.empty()) {
        auto id = freelist.back();
        freelist.pop_back();
        return id;
    }

    debug_assert(last_id < max_id);

    return GpuDescriptorId(++last_id);
}

void GpuDescriptorIdAllocator::free(GpuDescriptorId id)
{
    if (id) {
        freelist.emplace_back(id);
    }
}

// -----------------------------------------------------------------------------

void gpu_allocate_image_descriptor(GpuImageBase* image)
{
    auto* gpu = image->gpu;
    auto& vk = gpu->vk;

    auto id = gpu->image_descriptor_allocator.allocate();

    image->data.id = id;

    auto usage = gpu_image_usage_to_vk(image->usage());

    if (usage & VK_IMAGE_USAGE_SAMPLED_BIT) {
        vk.UpdateDescriptorSets(gpu->device, 1, std::array {
            VkWriteDescriptorSet {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = gpu->set,
                .dstBinding = 0,
                .dstArrayElement = id.value,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                .pImageInfo = ptr_to(VkDescriptorImageInfo {
                    .imageView = image->view(),
                    .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
                }),
            },
        }.data(), 0, nullptr);
    }

    if (usage & VK_IMAGE_USAGE_STORAGE_BIT) {
        vk.UpdateDescriptorSets(gpu->device, 1, std::array {
            VkWriteDescriptorSet {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = gpu->set,
                .dstBinding = 1,
                .dstArrayElement = id.value,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                .pImageInfo = ptr_to(VkDescriptorImageInfo {
                    .imageView = image->view(),
                    .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
                }),
            },
        }.data(), 0, nullptr);
    }
}

// -----------------------------------------------------------------------------

void gpu_allocate_sampler_descriptor(GpuSampler* sampler)
{
    auto* gpu = sampler->gpu;
    auto& vk = gpu->vk;

    auto id = gpu->sampler_descriptor_allocator.allocate();

    sampler->id = id;

    log_debug("Sampler allocated ID: {}", sampler->id.value);

    vk.UpdateDescriptorSets(gpu->device, 1, std::array {
        VkWriteDescriptorSet {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = gpu->set,
            .dstBinding = 2,
            .dstArrayElement = sampler->id.value,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
            .pImageInfo = ptr_to(VkDescriptorImageInfo {
                .sampler = sampler->sampler,
            }),
        },
    }.data(), 0, nullptr);
}
