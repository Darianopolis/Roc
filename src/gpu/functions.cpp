#include "functions.hpp"
#include "gpu.hpp"

#include "core/log.hpp"

#define VULKAN_LOAD_INSTANCE_FUNCTION(FuncName, ...) \
    gpu->vk.FuncName = (PFN_vk##FuncName)gpu->vk.GetInstanceProcAddr(gpu->instance, "vk"#FuncName); \
    if (!gpu->vk.FuncName) log_error("Failed to load vk" #FuncName);
#define VULKAN_LOAD_DEVICE_FUNCTION(  FuncName, ...) \
    gpu->vk.FuncName = (PFN_vk##FuncName)gpu->vk.GetDeviceProcAddr(  gpu->device,   "vk"#FuncName); \
    if (!gpu->vk.FuncName) log_error("Failed to load vk" #FuncName);

void gpu_init_functions(gpu::Context* gpu, PFN_vkGetInstanceProcAddr loadFn)
{
    gpu->vk.GetInstanceProcAddr = loadFn;

    VULKAN_LOAD_INSTANCE_FUNCTION(CreateInstance)
}

void gpu_load_instance_functions(gpu::Context* gpu)
{
    GPU_INSTANCE_FUNCTIONS(VULKAN_LOAD_INSTANCE_FUNCTION)
}

void gpu_load_device_functions(gpu::Context* gpu)
{
    GPU_DEVICE_FUNCTIONS(VULKAN_LOAD_DEVICE_FUNCTION)
}
