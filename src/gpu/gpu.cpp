#include "internal.hpp"

#include "core/types.hpp"
#include "core/util.hpp"
#include "core/process.hpp"

#include <sys/sysmacros.h>

const char* gpu_result_to_string(VkResult res)
{
    return string_VkResult(res);
}

gpu::Context::~Context()
{
    log_info("GPU context destroyed");

    graphics_queue = nullptr;
    transfer_queue = nullptr;

    core_assert(stats.active_images == 0);
    core_assert(stats.active_buffers == 0);
    core_assert(stats.active_samplers == 0);

    vmaDestroyAllocator(vma);

    for (auto* binary_sema : free_binary_semaphores) {
        vk.DestroySemaphore(device, binary_sema, nullptr);
    }

    vk.DestroyPipelineLayout(device, pipeline_layout, nullptr);
    vk.DestroyDescriptorSetLayout(device, set_layout, nullptr);
    vk.DestroyDescriptorPool(device, pool, nullptr);

    vk.DestroyDevice(device, nullptr);
    vk.DestroyDebugUtilsMessengerEXT(instance, debug_messenger, nullptr);
    vk.DestroyInstance(instance, nullptr);

    drmFreeDevice(&drm.device);
    close(drm.fd);
}

static
void load_renderdoc(gpu::Context* gpu)
{
    log_debug("Loading RenderDoc API");

    void* mod = dlopen("librenderdoc.so", RTLD_NOW | RTLD_NOLOAD);
    if (!mod) {
        log_error("Failed to load shared object: [librenderdoc.so]");
        return;
    }

    auto RENDERDOC_GetAPI = (pRENDERDOC_GetAPI)dlsym(mod, "RENDERDOC_GetAPI");
    if (!RENDERDOC_GetAPI) {
        log_error("Failed to load symbol: [RENDERDOC_GetAPI]");
        return;
    }

    RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_7_0, (void**)&gpu->renderdoc);

    int major, minor, patch;
    gpu->renderdoc->GetAPIVersion(&major, &minor, &patch);

    log_debug("RenderDoc API loaded: {}.{}.{}", major, minor, patch);
}

static
std::array required_device_extensions = {
    VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
    VK_KHR_MAINTENANCE_5_EXTENSION_NAME,
    VK_KHR_GLOBAL_PRIORITY_EXTENSION_NAME,
    VK_KHR_UNIFIED_IMAGE_LAYOUTS_EXTENSION_NAME,
    VK_EXT_SHADER_OBJECT_EXTENSION_NAME,

    VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
    VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME,
    VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME,
    VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
    VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME,
    VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME,
};

static
bool open_drm(gpu::Context* gpu, drmDevice* device)
{
    // Prefer to open the render node for normal render operations,
    // even the requested drm was opened from a primary node

    const char* name = nullptr;
    if (device->available_nodes & (1 << DRM_NODE_RENDER)) {
        name = device->nodes[DRM_NODE_RENDER];
    } else if (device->available_nodes & (1 << DRM_NODE_PRIMARY)) {
        name = device->nodes[DRM_NODE_PRIMARY];
        log_warn("  device has no render node, falling back to primary node");
    } else {
        log_warn("  device has no render or primary nodes, ignoring.");
        return false;
    }

    log_debug("  drm path: {}", name);
    gpu->drm.fd = core::check<open>(name, O_RDWR | O_NONBLOCK | O_CLOEXEC).value;
    log_debug("  drm fd: {}", gpu->drm.fd);

    if (gpu->drm.fd) {
        gpu->drm.device = device;
        struct stat drm_stat;
        stat(name, &drm_stat);
        log_debug("  drm id: {}", drm_stat.st_rdev);
        gpu->drm.id = drm_stat.st_rdev;
        return true;
    }
    return false;
}

static
bool try_physical_device(gpu::Context* gpu, VkPhysicalDevice phdev)
{
    {
        VkPhysicalDeviceProperties2 props { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
        gpu->vk.GetPhysicalDeviceProperties2(phdev, &props);
        log_debug("Testing physical device: {}", props.properties.deviceName);
    }

    // Device extension support

    std::vector<VkExtensionProperties> available_extensions;
    gpu_vk_enumerate(available_extensions, gpu->vk.EnumerateDeviceExtensionProperties, phdev, nullptr);

    auto check_extension = [&](const char* name) -> bool {
        for (auto& extension : available_extensions) {
            if (strcmp(extension.extensionName, name) == 0) {
                return true;
            }
        }
        return false;
    };

    for (auto& ext : required_device_extensions) {
        if (!check_extension(ext)) {
            log_warn("  device mission extension: {}", ext);
            return false;
        }
    }

    // Match DRM device

    gpu->drm.fd = -1;

    if (check_extension(VK_EXT_PHYSICAL_DEVICE_DRM_EXTENSION_NAME) && false) {
        VkPhysicalDeviceDrmPropertiesEXT drm_props {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRM_PROPERTIES_EXT,
        };
        gpu->vk.GetPhysicalDeviceProperties2(phdev, core::ptr_to(VkPhysicalDeviceProperties2 {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
            .pNext = &drm_props,
        }));

        dev_t primary_dev_id = makedev(drm_props.primaryMajor, drm_props.primaryMinor);
        dev_t render_dev_id  = makedev(drm_props.renderMajor,  drm_props.renderMinor);

        if (!drm_props.hasPrimary && !drm_props.hasRender) {
            log_warn("  device has no primary or render node");
            return false;
        }

        drmDevice* device;
        core::check<drmGetDeviceFromDevId>(drm_props.hasRender ? render_dev_id : primary_dev_id, 0, &device);

        if (!open_drm(gpu, device)) {
            drmFreeDevice(&device);
            return false;
        }
    } else {
        auto num_devices = drmGetDevices2(0, nullptr, 0);
        std::vector<drmDevice*> devices(num_devices);
        num_devices = drmGetDevices2(0, devices.data(), devices.size());
        devices.resize(std::min(devices.size(), usz(num_devices)));
        defer {
            for (auto& device : devices) if (device) drmFreeDevice(&device);
        };

        VkPhysicalDevicePCIBusInfoPropertiesEXT pci_props = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PCI_BUS_INFO_PROPERTIES_EXT,
        };
        VkPhysicalDeviceProperties2 props {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
        };
        bool pci_info = check_extension(VK_EXT_PCI_BUS_INFO_EXTENSION_NAME);
        if (pci_info) props.pNext = &pci_props;
        gpu->vk.GetPhysicalDeviceProperties2(phdev, &props);

        if (check_extension(VK_EXT_PCI_BUS_INFO_EXTENSION_NAME)) {
            log_debug("Device does not support " VK_EXT_PHYSICAL_DEVICE_DRM_EXTENSION_NAME );
            log_debug("Matching DRM against " VK_EXT_PCI_BUS_INFO_EXTENSION_NAME);
        } else {
            log_warn("Device does not support " VK_EXT_PHYSICAL_DEVICE_DRM_EXTENSION_NAME " or " VK_EXT_PCI_BUS_INFO_EXTENSION_NAME);
            log_warn("Matching DRM against vendorID/deviceID");
        }

        for (auto*& candidate : devices)  {
            if (pci_info) {
                if (pci_props.pciDomain   != candidate->businfo.pci->domain) continue;
                if (pci_props.pciBus      != candidate->businfo.pci->bus)    continue;
                if (pci_props.pciDevice   != candidate->businfo.pci->dev)    continue;
                if (pci_props.pciFunction != candidate->businfo.pci->func)   continue;
            } else {
                if (props.properties.vendorID != candidate->deviceinfo.pci->vendor_id) continue;
                if (props.properties.deviceID != candidate->deviceinfo.pci->device_id) continue;
            }

            if (open_drm(gpu, candidate)) {
                // Prevent candidate from being destroyed
                candidate = nullptr;
                break;
            }
        }
    }

    if (gpu->drm.fd == -1)  {
        log_warn("  failed to find or open DRM device, skipping...");
        return false;
    }

    log_info("  device selected");

    return true;
}

VkBool32 VKAPI_CALL debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    u32 type,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void* userdata)
{
    if (!data->pMessage) return VK_FALSE;

    core::LogLevel level;
    switch (severity) {
        break;case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT: level = core::LogLevel::trace;
        break;case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:    level = core::LogLevel::info;
        break;case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT: level = core::LogLevel::warn;
        break;case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:   level = core::LogLevel::error;
        break;case VK_DEBUG_UTILS_MESSAGE_SEVERITY_FLAG_BITS_MAX_ENUM_EXT:
            core::unreachable();
    }

    if (!core::is_log_level_enabled(level)) return VK_FALSE;

    if (data->messageIdNumber) {
        auto message = std::format("Validation {}: [ {} ] | MessageID = {:#x}\n",
            level == core::LogLevel::error ? "Error" : "Warning",
            data->pMessageIdName,
            data->messageIdNumber);
        message += data->pMessage;
        message += std::format("\nObjects: {}\n", data->objectCount);

        auto objects = std::span(data->pObjects, data->objectCount);

        // Compute max widths for printing
        usz max_index_width = std::log10(data->objectCount - 1) + 1;
        usz max_type_len = 0;
        for (auto& object : objects) {
            max_type_len = std::max(max_type_len, strlen(string_VkObjectType(object.objectType)));
        }

        // Print objects
        for (auto[i, object] : objects | std::views::enumerate) {
            auto prefix = sizeof("VK_OBJECT_TYPE_") - 1;
            auto type_name = std::string_view(string_VkObjectType(object.objectType)).substr(prefix);
            auto type_width = max_type_len - prefix;
            // clangd fails on the nested string width parameter, so we uese vformat directly
            message += std::vformat("    [{:{}}]: {:{}} {:#x}\n",
                std::make_format_args(i, max_index_width, type_name, type_width, object.objectHandle));
        }

        core::log(level, message);
    } else {
        core::log(level, data->pMessage);
    }

    if (severity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        core::debugkill();
    }

    return VK_FALSE;
}

core::Ref<gpu::Context> gpu::create(core::Flags<gpu::Feature> _features, core::EventLoop* event_loop)
{
    auto gpu = core::create<gpu::Context>();
    gpu->features = _features;

    gpu->event_loop = event_loop;

    // Loader

    gpu->loader = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!gpu->loader) {
        log_error("Failed to load vulkan library");
        return nullptr;
    }

    dlerror();

    // Instance

    VkDebugUtilsMessengerCreateInfoEXT debug_messenger_info = {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity
            = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
            | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType
            = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
            | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
            | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = debug_callback,
        .pUserData = gpu.get(),
    };

    auto vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(gpu->loader, "vkGetInstanceProcAddr"));
    if (!vkGetInstanceProcAddr) {
        log_error("Failed to load vulkan library");
        return nullptr;
    }

    gpu_init_functions(gpu.get(), vkGetInstanceProcAddr);

    std::vector<const char*> instance_extensions {
        VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
    };

    gpu_check(gpu->vk.CreateInstance(core::ptr_to(VkInstanceCreateInfo {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = core::ptr_to(VkValidationFeaturesEXT {
            .sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT,
            .pNext = &debug_messenger_info,
        }),
        .pApplicationInfo = core::ptr_to(VkApplicationInfo {
            .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .apiVersion = VK_API_VERSION_1_4,
        }),
        .enabledExtensionCount = u32(instance_extensions.size()),
        .ppEnabledExtensionNames = instance_extensions.data(),
    }), nullptr, &gpu->instance));

    gpu_load_instance_functions(gpu.get());

    gpu_check(gpu->vk.CreateDebugUtilsMessengerEXT(gpu->instance, &debug_messenger_info, nullptr, &gpu->debug_messenger));

    // Select GPU

    std::vector<VkPhysicalDevice> physical_devices;
    gpu_vk_enumerate(physical_devices, gpu->vk.EnumeratePhysicalDevices, gpu->instance);

    for (auto& phdev : physical_devices) {
        if (try_physical_device(gpu.get(), phdev)) {
            gpu->physical_device = phdev;
            break;
        }
    }

    if (!gpu->physical_device) {
        log_error("No suitable vulkan device found");
        return nullptr;
    }

    // Detect tooling

    {
        std::vector<VkPhysicalDeviceToolProperties> tools;
        gpu_vk_enumerate(tools, gpu->vk.GetPhysicalDeviceToolProperties, gpu->physical_device);

        for (auto& tool : tools) {
            if (tool.layer == "VK_LAYER_KHRONOS_validation"sv) {
                log_warn("Detected validation layers, enabling validation support");
                gpu->features |= gpu::Feature::validation;

            } else if (tool.name == "RenderDoc"sv) {
                load_renderdoc(gpu.get());
            }
        }
    }

    // Device creation

    static constexpr u32 invalid_index = ~0u;
    u32 graphics_queue_family = invalid_index;
    u32 transfer_queue_family = invalid_index;

    std::vector<VkQueueFamilyProperties> queue_props;
    gpu_vk_enumerate(queue_props, gpu->vk.GetPhysicalDeviceQueueFamilyProperties, gpu->physical_device);
    for (u32 i = 0; i < queue_props.size(); ++i) {
        if (queue_props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            if (graphics_queue_family == invalid_index) {
                graphics_queue_family = i;
            }
        } else if (queue_props[i].queueFlags & VK_QUEUE_TRANSFER_BIT) {
            if (transfer_queue_family == invalid_index) {
                transfer_queue_family = i;
            }
        }
    }

    auto create_device = [&](bool global_priority) {
        return gpu_check(gpu->vk.CreateDevice(gpu->physical_device, core::ptr_to(VkDeviceCreateInfo {
            .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            .pNext = gpu_vk_make_chain_in({
                core::ptr_to(VkPhysicalDeviceFeatures2 {
                    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
                    .features = {
                        .shaderInt64 = true,
                        .shaderInt16 = true,
                    },
                }),
                core::ptr_to(VkPhysicalDeviceVulkan11Features {
                    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
                    .storagePushConstant16 = true,
                    .samplerYcbcrConversion = true,
                    .shaderDrawParameters = true,
                }),
                core::ptr_to(VkPhysicalDeviceVulkan12Features {
                    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
                    .storagePushConstant8 = true,
                    .shaderInt8 = true,
                    .descriptorIndexing = true,
                    .shaderSampledImageArrayNonUniformIndexing = true,
                    .descriptorBindingSampledImageUpdateAfterBind = true,
                    .descriptorBindingStorageImageUpdateAfterBind = true,
                    .descriptorBindingUpdateUnusedWhilePending = true,
                    .descriptorBindingPartiallyBound = true,
                    .runtimeDescriptorArray = true,
                    .scalarBlockLayout = true,
                    .timelineSemaphore = true,
                    .bufferDeviceAddress = true,
                }),
                core::ptr_to(VkPhysicalDeviceVulkan13Features {
                    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
                    .synchronization2 = true,
                    .dynamicRendering = true,
                    .maintenance4 = true,
                }),
                core::ptr_to(VkPhysicalDeviceVulkan14Features {
                    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES,
                    .maintenance5 = true,
                    .maintenance6 = true,
                }),
                core::ptr_to(VkPhysicalDeviceUnifiedImageLayoutsFeaturesKHR {
                    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_UNIFIED_IMAGE_LAYOUTS_FEATURES_KHR,
                    .unifiedImageLayouts = true,
                    .unifiedImageLayoutsVideo = true,
                }),
                core::ptr_to(VkPhysicalDeviceShaderObjectFeaturesEXT {
                    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_OBJECT_FEATURES_EXT,
                    .shaderObject = true,
                }),
            }),
            .queueCreateInfoCount = 2,
            .pQueueCreateInfos = std::array {
                VkDeviceQueueCreateInfo {
                    .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                    .pNext =
                        global_priority
                            ? core::ptr_to(VkDeviceQueueGlobalPriorityCreateInfoKHR {
                                .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_GLOBAL_PRIORITY_CREATE_INFO,
                                .globalPriority = VK_QUEUE_GLOBAL_PRIORITY_HIGH,
                            })
                            : nullptr,
                    .queueFamilyIndex = graphics_queue_family,
                    .queueCount = 1,
                    .pQueuePriorities = core::ptr_to(1.f),
                },
                VkDeviceQueueCreateInfo {
                    .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                    .queueFamilyIndex = transfer_queue_family,
                    .queueCount = 1,
                    .pQueuePriorities = core::ptr_to(1.f),
                },
            }.data(),
            .enabledExtensionCount = u32(required_device_extensions.size()),
            .ppEnabledExtensionNames = required_device_extensions.data(),
        }), nullptr, &gpu->device), VK_ERROR_NOT_PERMITTED);
    };

    if (core::capability::has(CAP_SYS_NICE)) {
        log_debug("NICE system capability detected, requesting high global queue priority");
        if (create_device(true) == VK_ERROR_NOT_PERMITTED) {
            log_warn("Failed to acquire global queue priority, falling back to normal queue priorities");
            create_device(false);
        } else {
            log_info("Sucessfully created device with high global queue priority");
        }
        core::capability::drop(CAP_SYS_NICE);
    } else {
        create_device(false);
    }

    gpu_load_device_functions(gpu.get());

    gpu->graphics_queue = gpu_queue_init(gpu.get(), gpu::QueueType::graphics, graphics_queue_family);
    gpu->transfer_queue = gpu_queue_init(gpu.get(), gpu::QueueType::transfer, transfer_queue_family);

    // VMA allocator

    gpu_check(vmaCreateAllocator(core::ptr_to(VmaAllocatorCreateInfo {
        .flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT
               | VMA_ALLOCATOR_CREATE_EXTERNALLY_SYNCHRONIZED_BIT
               | VMA_ALLOCATOR_CREATE_KHR_MAINTENANCE4_BIT
               | VMA_ALLOCATOR_CREATE_KHR_MAINTENANCE5_BIT,
        .physicalDevice = gpu->physical_device,
        .device = gpu->device,
        .pVulkanFunctions = core::ptr_to(VmaVulkanFunctions {
            .vkGetInstanceProcAddr = gpu->vk.GetInstanceProcAddr,
            .vkGetDeviceProcAddr = gpu->vk.GetDeviceProcAddr,
        }),
        .instance = gpu->instance,
        .vulkanApiVersion = VK_API_VERSION_1_4,
    }), &gpu->vma));

    // State initialization

    gpu_init_descriptors(gpu.get());

    return gpu;
}
