#include "internal.hpp"

#include <core/stack.hpp>

struct GpuPipeline
{
    Gpu* gpu;

    VkPipeline pipeline;
    VkPipelineBindPoint bind_point;

    ~GpuPipeline()
    {
        gpu->vk.DestroyPipeline(gpu->device, pipeline, nullptr);
    }
};

auto gpu_pipeline_create_compute(Gpu* gpu, const GpuShaderStageInfo& shader_info) -> Ref<GpuPipeline>
{
    auto pipeline = ref_create<GpuPipeline>();
    pipeline->gpu = gpu;
    pipeline->bind_point = VK_PIPELINE_BIND_POINT_COMPUTE;

    gpu_check(gpu->vk.CreateComputePipelines(gpu->device, nullptr, 1, ptr_to(VkComputePipelineCreateInfo {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .pNext = ptr_to(VkShaderModuleCreateInfo {
                .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                .codeSize = shader_info.code.size_bytes(),
                .pCode = shader_info.code.data(),
            }),
            .stage = shader_info.stage,
            .pName = shader_info.entry,
        },
        .layout = gpu->pipeline_layout,
    }), nullptr, &pipeline->pipeline));

    return pipeline;
}

void gpu_dispatch(GpuCommands* cmd, vec3u32 extent)
{
    cmd->gpu->vk.CmdDispatch(cmd->buffer, extent.x, extent.y, extent.z);
}

auto gpu_pipeline_create(Gpu* gpu, const GpuGraphicsPipelineCreateInfo& info) -> Ref<GpuPipeline>
{
    ThreadStack stack;

    auto pipeline = ref_create<GpuPipeline>();
    pipeline->gpu = gpu;
    pipeline->bind_point = VK_PIPELINE_BIND_POINT_GRAPHICS;

    auto shaders = stack.allocate<VkPipelineShaderStageCreateInfo>(info.shaders.size());
    auto modules = stack.allocate<VkShaderModuleCreateInfo>(info.shaders.size());
    for (usz i = 0; i < info.shaders.size(); ++i) {
        modules[i] = {
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = info.shaders[i].code.size_bytes(),
            .pCode = info.shaders[i].code.data(),
        };
        shaders[i] = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .pNext = &modules[i],
            .stage = info.shaders[i].stage,
            .pName = info.shaders[i].entry,
        };
    }

    VkPipelineColorBlendAttachmentState blend = {
        .blendEnable = true,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT
                        | VK_COLOR_COMPONENT_G_BIT
                        | VK_COLOR_COMPONENT_B_BIT
                        | VK_COLOR_COMPONENT_A_BIT,
    };

    switch (info.blend_direction) {
        break;case GpuBlendDirection::back_to_front:
            blend.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
            blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        break;case GpuBlendDirection::front_to_back:
            blend.srcColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
            blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
            blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
            blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    }

    gpu_check(gpu->vk.CreateGraphicsPipelines(gpu->device, nullptr, 1, ptr_to(VkGraphicsPipelineCreateInfo {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = ptr_to(VkPipelineRenderingCreateInfo {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
            .colorAttachmentCount = 1,
            .pColorAttachmentFormats = ptr_to(info.format->vk),
        }),
        .stageCount = u32(info.shaders.size()),
        .pStages = shaders,
        .pVertexInputState = ptr_to(VkPipelineVertexInputStateCreateInfo {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        }),
        .pInputAssemblyState = ptr_to(VkPipelineInputAssemblyStateCreateInfo {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .topology = info.topology,
        }),
        .pViewportState = ptr_to(VkPipelineViewportStateCreateInfo {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        }),
        .pRasterizationState = ptr_to(VkPipelineRasterizationStateCreateInfo {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .lineWidth = 1.f,
        }),
        .pMultisampleState = ptr_to(VkPipelineMultisampleStateCreateInfo {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        }),
        .pDepthStencilState = nullptr,
        .pColorBlendState = ptr_to(VkPipelineColorBlendStateCreateInfo {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .attachmentCount = 1,
            .pAttachments = &blend,
        }),
        .pDynamicState = ptr_to(VkPipelineDynamicStateCreateInfo {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
            .dynamicStateCount = 2,
            .pDynamicStates = std::array {
                VK_DYNAMIC_STATE_SCISSOR_WITH_COUNT,
                VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT,
            }.data(),
        }),
        .layout = gpu->pipeline_layout,
    }), nullptr, &pipeline->pipeline));


    return pipeline;
}

// -----------------------------------------------------------------------------

void gpu_push_constants(GpuCommands* cmd, u32 offset, std::span<const byte> data)
{
    auto* gpu = cmd->gpu;
    debug_assert(offset + data.size() <= gpu_push_constant_size, "{} > {}", offset + data.size(), gpu_push_constant_size);
    gpu->vk.CmdPushConstants(cmd->buffer, gpu->pipeline_layout, VK_SHADER_STAGE_ALL, offset, data.size(), data.data());
}

void gpu_bind_pipeline(GpuCommands* cmd, GpuPipeline* pipeline)
{
    cmd->gpu->vk.CmdBindPipeline(cmd->buffer, pipeline->bind_point, pipeline->pipeline);
}

// -----------------------------------------------------------------------------

void gpu_set_scissors(GpuCommands* cmd, std::span<const rect2i32> scissors)
{
    ThreadStack stack;

    auto* vk_scissors = stack.allocate<VkRect2D>(scissors.size());
    for (u32 i = 0; i < scissors.size(); ++i) {
        auto r = scissors[i];
        if (r.extent.x < 0) { r.origin.x -= (r.extent.x *= -1); }
        if (r.extent.y < 0) { r.origin.y -= (r.extent.y *= -1); }
        vk_scissors[i] = VkRect2D {
            .offset{     r.origin.x,      r.origin.y  },
            .extent{ u32(r.extent.x), u32(r.extent.y) },
        };
    }
    cmd->gpu->vk.CmdSetScissorWithCount(cmd->buffer, u32(scissors.size()), vk_scissors);
}

void gpu_set_viewports(GpuCommands* cmd, std::span<const rect2f32> viewports)
{
    ThreadStack stack;

    auto* vk_viewports = stack.allocate<VkViewport>(viewports.size());
    for (u32 i = 0; i < viewports.size(); ++i) {
        vk_viewports[i] = VkViewport {
            .x = viewports[i].origin.x,
            .y = viewports[i].origin.y,
            .width = viewports[i].extent.x,
            .height = viewports[i].extent.y,
            .minDepth = 0.f,
            .maxDepth = 1.f
        };
    }
    cmd->gpu->vk.CmdSetViewportWithCount(cmd->buffer, u32(viewports.size()), vk_viewports);
}

void gpu_bind_index_buffer(GpuCommands* cmd, GpuBuffer* buffer, u32 offset, VkIndexType type)
{
    cmd->gpu->vk.CmdBindIndexBuffer(cmd->buffer, buffer->buffer, offset, type);
}

void gpu_draw_indexed(GpuCommands* cmd, const GpuDrawInfo& info)
{
    cmd->gpu->vk.CmdDrawIndexed(cmd->buffer, info.index_count, info.instance_count, info.first_index, info.vertex_offset, info.first_instance);
}

void gpu_begin_rendering(GpuCommands* cmd, const GpuRenderPassInfo& info)
{
    auto extent = info.target->base()->extent;

    cmd->gpu->vk.CmdBeginRendering(cmd->buffer, ptr_to(VkRenderingInfo {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = { {}, {extent.x, extent.y} },
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = ptr_to(VkRenderingAttachmentInfo {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = info.target->base()->view,
            .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
            .loadOp = info.clear_color
                ? VK_ATTACHMENT_LOAD_OP_CLEAR
                : VK_ATTACHMENT_LOAD_OP_LOAD,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = info.clear_color
                ? VkClearValue {
                    .color = {
                        .float32 = {
                            info.clear_color->x,
                            info.clear_color->y,
                            info.clear_color->z,
                            info.clear_color->w
                        }
                    }
                }
                : VkClearValue {},
        }),
    }));
}

void gpu_end_rendering(GpuCommands* cmd)
{
    cmd->gpu->vk.CmdEndRendering(cmd->buffer);
}

// -----------------------------------------------------------------------------

void gpu_barrier(GpuCommands* cmd,
    std::span<GpuResource* const> _reads,
    std::span<GpuResource* const> _writes)
{
    auto* gpu = cmd->gpu;

    auto to_set = [](std::span<GpuResource* const> resources) {
        ankerl::unordered_dense::set<GpuResource*> out;
        out.reserve(resources.size());
        for (auto& res : resources) {
            if (auto* image = dynamic_cast<GpuImage*>(res)) {
                out.insert(image->base());
            } else {
                out.insert(res);
            }
        }
        return out;
    };

    auto reads = to_set(_reads);
    auto writes = to_set(_writes);

    bool need_barrier =
            std::ranges::any_of(gpu->unbarriered_writes, [&](const auto& _pending) {
                auto pending = _pending.get();
                return pending && (reads.contains(pending) /* RAW */ || writes.contains(pending) /* WAW */);
            })
         || std::ranges::any_of(writes, [&](GpuResource* write) {
                return gpu->unbarriered_reads.contains(Weak(write)) /* WAR */;
            });

    // TODO: Granular barriers and QFOTs
    //
    //       For simplicity currently we just launch a single uber-barrier to cover everything
    //       For optimal granularity, we would track stages+access and ranges for every resource
    //       We also need to apply QFOTs for DMABUFs

    if (need_barrier) {
        gpu->vk.CmdPipelineBarrier2(cmd->buffer, ptr_to(VkDependencyInfo {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .memoryBarrierCount = 1,
            .pMemoryBarriers = ptr_to(VkMemoryBarrier2 {
                .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                .srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                .dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT,
            }),
        }));
        gpu->unbarriered_reads.clear();
        gpu->unbarriered_writes.clear();
    }

    gpu->unbarriered_reads.insert( reads.begin(),  reads.end());
    gpu->unbarriered_writes.insert(writes.begin(), writes.end());

    for (auto& read  : _reads)  gpu_protect(cmd, read);
    for (auto& write : _writes) gpu_protect(cmd, write);
}
