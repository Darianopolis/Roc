#include "internal.hpp"

#include "core/stack.hpp"

struct gpu_shader
{
    gpu_context* gpu;

    VkShaderStageFlagBits stage;
    VkShaderEXT shader = {};

    ~gpu_shader();
};

gpu_shader::~gpu_shader()
{
    gpu->vk.DestroyShaderEXT(gpu->device, shader, nullptr);
}

auto gpu_shader_create(gpu_context* gpu, const gpu_shader_create_info& info) -> core::Ref<gpu_shader>
{
    auto shader = core::create<gpu_shader>();
    shader->gpu = gpu;
    shader->stage = info.stage;

    core::Flags<VkShaderStageFlagBits> next_stages = {};
    switch (info.stage) {
        break;case VK_SHADER_STAGE_VERTEX_BIT:
            next_stages = VK_SHADER_STAGE_FRAGMENT_BIT;
        break;default:
            ;
    }

    gpu_check(gpu->vk.CreateShadersEXT(gpu->device, 1, core::ptr_to(VkShaderCreateInfoEXT {
        .sType = VK_STRUCTURE_TYPE_SHADER_CREATE_INFO_EXT,
        .stage = info.stage,
        .nextStage = next_stages.get(),
        .codeType = VK_SHADER_CODE_TYPE_SPIRV_EXT,
        .codeSize = info.code.size() * 4,
        .pCode = info.code.data(),
        .pName = info.entry,
        .setLayoutCount = 1,
        .pSetLayouts = &gpu->set_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = core::ptr_to(VkPushConstantRange {
            .stageFlags = VK_SHADER_STAGE_ALL,
            .size = gpu_push_constant_size,
        }),
    }), nullptr, &shader->shader));

    return shader;
}

void gpu_cmd_push_constants(gpu_commands* cmd, u32 offset, core::ByteView data)
{
    auto* gpu = cmd->queue->gpu;
    gpu->vk.CmdPushConstants(cmd->buffer, gpu->pipeline_layout, VK_SHADER_STAGE_ALL, offset, data.size, data.data);
}

void gpu_cmd_set_scissors(gpu_commands* cmd, std::span<const rect2i32> scissors)
{
    auto* gpu = cmd->queue->gpu;

    core::ThreadStack stack;

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
    gpu->vk.CmdSetScissorWithCount(cmd->buffer, u32(scissors.size()), vk_scissors);
}

void gpu_cmd_set_viewports(gpu_commands* cmd, std::span<const rect2f32> viewports)
{
    auto* gpu = cmd->queue->gpu;

    core::ThreadStack stack;

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
    gpu->vk.CmdSetViewportWithCount(cmd->buffer, u32(viewports.size()), vk_viewports);
}

void gpu_cmd_set_polygon_state(gpu_commands* cmd, VkPrimitiveTopology topology, VkPolygonMode polygon_mode, f32 line_width)
{
    auto* gpu = cmd->queue->gpu;

    gpu->vk.CmdSetPrimitiveTopology(cmd->buffer, topology);
    gpu->vk.CmdSetPolygonModeEXT(   cmd->buffer, polygon_mode);
    gpu->vk.CmdSetLineWidth(        cmd->buffer, line_width);
}

void gpu_cmd_set_cull_state(gpu_commands* cmd, VkCullModeFlagBits cull_mode, VkFrontFace front_face)
{
    auto* gpu = cmd->queue->gpu;

    gpu->vk.CmdSetCullMode( cmd->buffer, cull_mode);
    gpu->vk.CmdSetFrontFace(cmd->buffer, front_face);
}

void gpu_cmd_set_depth_state(gpu_commands* cmd, core::Flags<gpu_depth_enable> enabled, VkCompareOp compare_op)
{
    auto* gpu = cmd->queue->gpu;

    gpu->vk.CmdSetDepthTestEnable( cmd->buffer, enabled.contains(gpu_depth_enable::test));
    gpu->vk.CmdSetDepthWriteEnable(cmd->buffer, enabled.contains(gpu_depth_enable::write));
    gpu->vk.CmdSetDepthCompareOp(  cmd->buffer, compare_op);
}

void gpu_cmd_set_blend_state(gpu_commands* cmd, std::span<const gpu_blend_mode> blends)
{
    auto* gpu = cmd->queue->gpu;

    auto count = u32(blends.size());

    core::ThreadStack stack;

    auto* components         = stack.allocate<VkColorComponentFlags>(  count);
    auto* blend_enable_bools = stack.allocate<VkBool32>(               count);
    auto* blend_equations    = stack.allocate<VkColorBlendEquationEXT>(count);

    for (u32 i = 0; i < count; ++i) {
        components[i] = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
            | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        blend_enable_bools[i] = blends[i] != gpu_blend_mode::none;

        switch (blends[i]) {
            break;case gpu_blend_mode::none:
                blend_equations[i] = {
                    .srcColorBlendFactor = VK_BLEND_FACTOR_ZERO,
                    .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
                    .colorBlendOp = VK_BLEND_OP_ADD,
                    .srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
                    .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
                    .alphaBlendOp = VK_BLEND_OP_ADD,
                };
            break;case gpu_blend_mode::postmultiplied:
                blend_equations[i] = {
                    .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
                    .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                    .colorBlendOp = VK_BLEND_OP_ADD,
                    .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
                    .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                    .alphaBlendOp = VK_BLEND_OP_ADD,
                };
            break;case gpu_blend_mode::premultiplied:
                blend_equations[i] = {
                    .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
                    .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                    .colorBlendOp = VK_BLEND_OP_ADD,
                    .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
                    .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                    .alphaBlendOp = VK_BLEND_OP_ADD,
                };
        }
    }

    gpu->vk.CmdSetColorBlendEnableEXT(  cmd->buffer, 0, count, blend_enable_bools);
    gpu->vk.CmdSetColorWriteMaskEXT(    cmd->buffer, 0, count, components);
    gpu->vk.CmdSetColorBlendEquationEXT(cmd->buffer, 0, count, blend_equations);
}

void gpu_cmd_bind_shaders(gpu_commands* cmd, std::span<gpu_shader* const> shaders)
{
    auto* gpu = cmd->queue->gpu;

    u32 count = u32(shaders.size());

    core::ThreadStack stack;

    auto* stage_flags    = stack.allocate<VkShaderStageFlagBits>(count);
    auto* shader_objects = stack.allocate<VkShaderEXT>(count);

    for (u32 i = 0; i < shaders.size(); ++i) {
        stage_flags[i] = VkShaderStageFlagBits(shaders[i]->stage);
        shader_objects[i] = shaders[i]->shader;
    }

    gpu->vk.CmdBindShadersEXT(cmd->buffer, count, stage_flags, shader_objects);
}

void gpu_cmd_reset_graphics_state(gpu_commands* cmd)
{
    auto* gpu = cmd->queue->gpu;

    gpu->vk.CmdSetAlphaToCoverageEnableEXT(cmd->buffer, false);
    gpu->vk.CmdSetSampleMaskEXT(           cmd->buffer, VK_SAMPLE_COUNT_1_BIT, core::ptr_to<VkSampleMask>(0xFFFF'FFFF));
    gpu->vk.CmdSetRasterizationSamplesEXT( cmd->buffer, VK_SAMPLE_COUNT_1_BIT);
    gpu->vk.CmdSetVertexInputEXT(          cmd->buffer, 0, nullptr, 0, nullptr);

    gpu->vk.CmdSetRasterizerDiscardEnable(cmd->buffer, false);
    gpu->vk.CmdSetPrimitiveRestartEnable( cmd->buffer, false);

    // Stencil tests

    gpu->vk.CmdSetStencilTestEnable(cmd->buffer, false);
    gpu->vk.CmdSetStencilOp(        cmd->buffer, VK_STENCIL_FACE_FRONT_AND_BACK,
        VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_COMPARE_OP_NEVER);

    // Depth (extended)

    gpu->vk.CmdSetDepthBiasEnable(      cmd->buffer, false);
    gpu->vk.CmdSetDepthBoundsTestEnable(cmd->buffer, false);
    gpu->vk.CmdSetDepthBounds(          cmd->buffer, 0.f, 1.f);

    gpu_cmd_set_polygon_state(cmd, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_POLYGON_MODE_FILL, 1.f);
    gpu_cmd_set_cull_state(   cmd, VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE);
    gpu_cmd_set_depth_state(   cmd, {}, VK_COMPARE_OP_ALWAYS);
}
