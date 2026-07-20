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

// -----------------------------------------------------------------------------

void gpu_push_constants(GpuCommands* cmd, u32 offset, std::span<const byte> data)
{
    auto* gpu = cmd->gpu;
    debug_assert(offset + data.size() <= gpu_push_constant_size, "{} > {}", offset + data.size(), gpu_push_constant_size);
    gpu->vk.CmdPushConstants(cmd->buffer, gpu->pipeline_layout, VK_SHADER_STAGE_ALL, offset, num_cast<u32>(data.size()), data.data());
}

void gpu_bind_pipeline(GpuCommands* cmd, GpuPipeline* pipeline)
{
    cmd->gpu->vk.CmdBindPipeline(cmd->buffer, pipeline->bind_point, pipeline->pipeline);
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
