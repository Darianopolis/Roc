#pragma once

#include <core/pch.hpp>

#define GPU_INSTANCE_FUNCTIONS(DO) \
    DO(EnumeratePhysicalDevices) \
    DO(GetPhysicalDeviceProperties2) \
    DO(GetPhysicalDeviceQueueFamilyProperties) \
    DO(EnumerateDeviceExtensionProperties) \
    DO(CreateDevice) \
    DO(GetDeviceProcAddr) \
    DO(DestroyDevice) \
    DO(DestroyInstance) \
    DO(GetPhysicalDeviceMemoryProperties) \
    DO(GetPhysicalDeviceFormatProperties2) \
    DO(GetPhysicalDeviceImageFormatProperties2) \
    DO(GetPhysicalDeviceToolProperties) \
    DO(CreateDebugUtilsMessengerEXT) \
    DO(DestroyDebugUtilsMessengerEXT) \

#define GPU_DEVICE_FUNCTIONS(DO) \
    DO(GetDeviceQueue) \
    DO(CreateCommandPool) \
    DO(AllocateCommandBuffers) \
    DO(FreeCommandBuffers) \
    DO(CreateSemaphore) \
    DO(CreatePipelineLayout) \
    DO(CreateDescriptorPool) \
    DO(WaitForFences) \
    DO(ResetFences) \
    DO(QueueWaitIdle) \
    DO(DestroyImageView) \
    DO(CreateImageView) \
    DO(CmdPipelineBarrier2) \
    DO(BeginCommandBuffer) \
    DO(CmdBeginRendering) \
    DO(CmdBindIndexBuffer) \
    DO(CmdDraw) \
    DO(CmdDrawIndexed) \
    DO(CmdEndRendering) \
    DO(EndCommandBuffer) \
    DO(QueueSubmit2) \
    DO(DestroyCommandPool) \
    DO(DestroySemaphore) \
    DO(DestroyPipelineLayout) \
    DO(CreateFence) \
    DO(DestroyFence) \
    DO(DestroyDescriptorPool) \
    DO(CmdClearColorImage) \
    DO(ResetCommandPool) \
    DO(CreateBuffer) \
    DO(GetBufferMemoryRequirements) \
    DO(AllocateMemory) \
    DO(BindBufferMemory) \
    DO(DestroyBuffer) \
    DO(FreeMemory) \
    DO(MapMemory) \
    DO(GetBufferDeviceAddress) \
    DO(CmdPushConstants) \
    DO(DestroyImage) \
    DO(CreateImage) \
    DO(GetImageMemoryRequirements) \
    DO(BindImageMemory) \
    DO(CmdCopyImage2) \
    DO(CmdCopyBufferToImage) \
    DO(CmdCopyImageToBuffer) \
    DO(CreateSampler) \
    DO(DestroySampler) \
    DO(CreateDescriptorSetLayout) \
    DO(AllocateDescriptorSets) \
    DO(DestroyDescriptorSetLayout) \
    DO(UpdateDescriptorSets) \
    DO(CmdBindDescriptorSets) \
    DO(SetDebugUtilsObjectNameEXT) \
    DO(CmdBlitImage2) \
    DO(GetMemoryFdPropertiesKHR) \
    DO(GetImageMemoryRequirements2) \
    DO(BindImageMemory2) \
    DO(ImportSemaphoreFdKHR) \
    DO(GetSemaphoreFdKHR) \
    DO(GetImageDrmFormatModifierPropertiesEXT) \
    DO(GetImageSubresourceLayout) \
    DO(GetMemoryFdKHR) \
    DO(CmdDispatch) \
    DO(CmdSetViewportWithCount) \
    DO(CmdSetScissorWithCount) \
    DO(GetSemaphoreCounterValue) \
    DO(CreateGraphicsPipelines) \
    DO(DestroyPipeline) \
    DO(CmdBindPipeline) \
    DO(CreateComputePipelines) \

#define GPU_DECLARE_FUNCTION(funcName, ...) PFN_vk##funcName funcName;

struct GpuVulkanFunctions {
    GPU_DECLARE_FUNCTION(GetInstanceProcAddr)
    GPU_DECLARE_FUNCTION(CreateInstance)
    GPU_INSTANCE_FUNCTIONS(GPU_DECLARE_FUNCTION)
    GPU_DEVICE_FUNCTIONS(  GPU_DECLARE_FUNCTION)
};

void gpu_init_functions(         GpuVulkanFunctions&, PFN_vkGetInstanceProcAddr);
void gpu_load_instance_functions(GpuVulkanFunctions&, VkInstance);
void gpu_load_device_functions(  GpuVulkanFunctions&, VkDevice);
