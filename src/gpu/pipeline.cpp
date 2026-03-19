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

auto gpu_shader_create(gpu_context* gpu, const gpu_shader_create_info& info) -> ref<gpu_shader>
{
    auto shader = core_create<gpu_shader>();
    shader->gpu = gpu;
    shader->stage = info.stage;

    flags<VkShaderStageFlagBits> next_stages = {};
    switch (info.stage) {
        break;case VK_SHADER_STAGE_VERTEX_BIT:
            next_stages = VK_SHADER_STAGE_FRAGMENT_BIT;
        break;default:
            ;
    }

    gpu_check(gpu->vk.CreateShadersEXT(gpu->device, 1, ptr_to(VkShaderCreateInfoEXT {
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
        .pPushConstantRanges = ptr_to(VkPushConstantRange {
            .stageFlags = VK_SHADER_STAGE_ALL,
            .size = gpu_push_constant_size,
        }),
    }), nullptr, &shader->shader));

    return shader;
}

// -----------------------------------------------------------------------------

void gpu_renderpass::push_constants(u32 offset, core_byte_view data)
{
    core_assert(offset + data.size <= gpu_push_constant_size, "{} > {}", offset + data.size, gpu_push_constant_size);
    gpu->vk.CmdPushConstants(cmd, gpu->pipeline_layout, VK_SHADER_STAGE_ALL, offset, data.size, data.data);
}

void gpu_renderpass::set_scissors(std::span<const rect2i32> scissors)
{
    core_thread_stack stack;

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
    gpu->vk.CmdSetScissorWithCount(cmd, u32(scissors.size()), vk_scissors);
}

void gpu_renderpass::set_viewports(std::span<const rect2f32> viewports)
{
    core_thread_stack stack;

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
    gpu->vk.CmdSetViewportWithCount(cmd, u32(viewports.size()), vk_viewports);
}

void gpu_renderpass::set_polygon_state(VkPrimitiveTopology topology, VkPolygonMode polygon_mode, f32 line_width)
{
    gpu->vk.CmdSetPrimitiveTopology(cmd, topology);
    gpu->vk.CmdSetPolygonModeEXT(   cmd, polygon_mode);
    gpu->vk.CmdSetLineWidth(        cmd, line_width);
}

void gpu_renderpass::set_cull_state(VkCullModeFlagBits cull_mode, VkFrontFace front_face)
{
    gpu->vk.CmdSetCullMode( cmd, cull_mode);
    gpu->vk.CmdSetFrontFace(cmd, front_face);
}

void gpu_renderpass::set_depth_state(flags<gpu_depth_enable> enabled, VkCompareOp compare_op)
{
    gpu->vk.CmdSetDepthTestEnable( cmd, enabled.contains(gpu_depth_enable::test));
    gpu->vk.CmdSetDepthWriteEnable(cmd, enabled.contains(gpu_depth_enable::write));
    gpu->vk.CmdSetDepthCompareOp(  cmd, compare_op);
}

void gpu_renderpass::set_blend_state(std::span<const gpu_blend_mode> blends)
{
    auto count = u32(blends.size());

    core_thread_stack stack;

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

    gpu->vk.CmdSetColorBlendEnableEXT(  cmd, 0, count, blend_enable_bools);
    gpu->vk.CmdSetColorWriteMaskEXT(    cmd, 0, count, components);
    gpu->vk.CmdSetColorBlendEquationEXT(cmd, 0, count, blend_equations);
}

void gpu_renderpass::bind_shaders(std::span<gpu_shader* const> shaders)
{
    u32 count = u32(shaders.size());

    core_thread_stack stack;

    auto* stage_flags    = stack.allocate<VkShaderStageFlagBits>(count);
    auto* shader_objects = stack.allocate<VkShaderEXT>(count);

    for (u32 i = 0; i < shaders.size(); ++i) {
        stage_flags[i] = VkShaderStageFlagBits(shaders[i]->stage);
        shader_objects[i] = shaders[i]->shader;
    }

    gpu->vk.CmdBindShadersEXT(cmd, count, stage_flags, shader_objects);
}

void gpu_renderpass::bind_index_buffer(gpu_buffer* buffer, u32 offset, VkIndexType type)
{
    gpu->vk.CmdBindIndexBuffer(cmd, buffer->buffer, offset, type);
}

void gpu_renderpass::draw_indexed(const gpu_draw_info& info)
{
    gpu->vk.CmdDrawIndexed(cmd, info.index_count, info.instance_count, info.first_index, info.vertex_offset, info.first_instance);
}

static
void reset_graphics_state(gpu_renderpass& pass)
{
    auto[gpu, cmd] = pass;

    gpu->vk.CmdSetAlphaToCoverageEnableEXT(cmd, false);
    gpu->vk.CmdSetSampleMaskEXT(           cmd, VK_SAMPLE_COUNT_1_BIT, ptr_to<VkSampleMask>(0xFFFF'FFFF));
    gpu->vk.CmdSetRasterizationSamplesEXT( cmd, VK_SAMPLE_COUNT_1_BIT);
    gpu->vk.CmdSetVertexInputEXT(          cmd, 0, nullptr, 0, nullptr);

    gpu->vk.CmdSetRasterizerDiscardEnable(cmd, false);
    gpu->vk.CmdSetPrimitiveRestartEnable( cmd, false);

    // Stencil tests

    gpu->vk.CmdSetStencilTestEnable(cmd, false);
    gpu->vk.CmdSetStencilOp(        cmd, VK_STENCIL_FACE_FRONT_AND_BACK,
        VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_COMPARE_OP_NEVER);

    // Depth (extended)

    gpu->vk.CmdSetDepthBiasEnable(      cmd, false);
    gpu->vk.CmdSetDepthBoundsTestEnable(cmd, false);
    gpu->vk.CmdSetDepthBounds(          cmd, 0.f, 1.f);

    pass.set_polygon_state(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_POLYGON_MODE_FILL, 1.f);
    pass.set_cull_state(   VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE);
    pass.set_depth_state(  {}, VK_COMPARE_OP_ALWAYS);
}

auto gpu_renderpass_begin(gpu_context* gpu, const gpu_renderpass_info& info) -> gpu_renderpass
{
    auto cmd = gpu_get_commands(gpu)->buffer;

    gpu_protect(gpu, info.target);

    gpu_renderpass pass{gpu, cmd};

    reset_graphics_state(pass);

    auto extent = info.target->extent();

    gpu->vk.CmdBeginRendering(cmd, ptr_to(VkRenderingInfo {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = { {}, {extent.x, extent.y} },
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = ptr_to(VkRenderingAttachmentInfo {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = info.target->view(),
            .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = {
                .color = {
                    .float32 = {
                        info.clear_color.r,
                        info.clear_color.g,
                        info.clear_color.b,
                        info.clear_color.a
                    }
                }
            },
        }),
    }));

    gpu->vk.CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, gpu->pipeline_layout, 0, 1, &gpu->set, 0, nullptr);

    return pass;
}

void gpu_renderpass_end(gpu_renderpass& pass)
{
    auto[gpu, cmd] = pass;

    gpu->vk.CmdEndRendering(cmd);
}
