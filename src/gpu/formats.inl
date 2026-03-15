#include "internal.hpp"

using enum gpu::vk::FormatFlag;

struct gpu_drm_vk_format_pair {
    gpu::DrmFormat  drm;
    VkFormat         vk;
    core::Flags<gpu::vk::FormatFlag> flags;
};

gpu_drm_vk_format_pair drm_to_vk[] {
    // Vulkan non-packed 8-bits-per-channel formats have an inverted channel
    // order compared to the DRM formats, because DRM format channel order
    // is little-endian while Vulkan format channel order is in memory byte
    // order.
    { DRM_FORMAT_R8,       VK_FORMAT_R8_UNORM                     },
    { DRM_FORMAT_R16F,     VK_FORMAT_R16_SFLOAT                   },
    { DRM_FORMAT_R32F,     VK_FORMAT_R32_SFLOAT                   },
    { DRM_FORMAT_GR88,     VK_FORMAT_R8G8_UNORM                   },
    { DRM_FORMAT_GR1616F,  VK_FORMAT_R16G16_SFLOAT                },
    { DRM_FORMAT_GR3232F,  VK_FORMAT_R32G32_SFLOAT                },
    { DRM_FORMAT_RGB888,   VK_FORMAT_B8G8R8_UNORM                 },
    { DRM_FORMAT_BGR888,   VK_FORMAT_R8G8B8_UNORM                 },
    { DRM_FORMAT_ARGB8888, VK_FORMAT_B8G8R8A8_UNORM               },
    { DRM_FORMAT_XRGB8888, VK_FORMAT_B8G8R8A8_UNORM, ignore_alpha },
    { DRM_FORMAT_ABGR8888, VK_FORMAT_R8G8B8A8_UNORM               },
    { DRM_FORMAT_XBGR8888, VK_FORMAT_R8G8B8A8_UNORM, ignore_alpha },

    // Vulkan packed formats have the same channel order as DRM formats on
    // little endian systems.
    { DRM_FORMAT_RGBA4444,    VK_FORMAT_R4G4B4A4_UNORM_PACK16                  },
    { DRM_FORMAT_RGBX4444,    VK_FORMAT_R4G4B4A4_UNORM_PACK16,    ignore_alpha },
    { DRM_FORMAT_BGRA4444,    VK_FORMAT_B4G4R4A4_UNORM_PACK16                  },
    { DRM_FORMAT_BGRX4444,    VK_FORMAT_B4G4R4A4_UNORM_PACK16,    ignore_alpha },
    { DRM_FORMAT_RGB565,      VK_FORMAT_R5G6B5_UNORM_PACK16                    },
    { DRM_FORMAT_BGR565,      VK_FORMAT_B5G6R5_UNORM_PACK16                    },
    { DRM_FORMAT_RGBA5551,    VK_FORMAT_R5G5B5A1_UNORM_PACK16                  },
    { DRM_FORMAT_RGBX5551,    VK_FORMAT_R5G5B5A1_UNORM_PACK16,    ignore_alpha },
    { DRM_FORMAT_BGRA5551,    VK_FORMAT_B5G5R5A1_UNORM_PACK16                  },
    { DRM_FORMAT_BGRX5551,    VK_FORMAT_B5G5R5A1_UNORM_PACK16,    ignore_alpha },
    { DRM_FORMAT_ARGB1555,    VK_FORMAT_A1R5G5B5_UNORM_PACK16                  },
    { DRM_FORMAT_XRGB1555,    VK_FORMAT_A1R5G5B5_UNORM_PACK16,    ignore_alpha },
    { DRM_FORMAT_ARGB2101010, VK_FORMAT_A2R10G10B10_UNORM_PACK32               },
    { DRM_FORMAT_XRGB2101010, VK_FORMAT_A2R10G10B10_UNORM_PACK32, ignore_alpha },
    { DRM_FORMAT_ABGR2101010, VK_FORMAT_A2B10G10R10_UNORM_PACK32               },
    { DRM_FORMAT_XBGR2101010, VK_FORMAT_A2B10G10R10_UNORM_PACK32, ignore_alpha },

    // Vulkan 16-bits-per-channel formats have an inverted channel order
    // compared to DRM formats, just like the 8-bits-per-channel ones.
    // On little endian systems the memory representation of each channel
    // matches the DRM formats'.
    { DRM_FORMAT_BGR161616,     VK_FORMAT_R16G16B16_UNORM                   },
    { DRM_FORMAT_BGR161616F,    VK_FORMAT_R16G16B16_SFLOAT                  },
    { DRM_FORMAT_ABGR16161616,  VK_FORMAT_R16G16B16A16_UNORM                },
    { DRM_FORMAT_XBGR16161616,  VK_FORMAT_R16G16B16A16_UNORM,  ignore_alpha },
    { DRM_FORMAT_ABGR16161616F, VK_FORMAT_R16G16B16A16_SFLOAT               },
    { DRM_FORMAT_XBGR16161616F, VK_FORMAT_R16G16B16A16_SFLOAT, ignore_alpha },
    { DRM_FORMAT_BGR323232F,    VK_FORMAT_R32G32B32_SFLOAT                  },
    { DRM_FORMAT_ABGR32323232F, VK_FORMAT_R32G32B32A32_SFLOAT               },

    // YCbCr formats
    // R -> V, G -> Y, B -> U
    // 420 -> 2x2 subsampled, 422 -> 2x1 subsampled, 444 -> non-subsampled
    { DRM_FORMAT_UYVY,   VK_FORMAT_B8G8R8G8_422_UNORM        },
    { DRM_FORMAT_YUYV,   VK_FORMAT_G8B8G8R8_422_UNORM        },
    { DRM_FORMAT_NV12,   VK_FORMAT_G8_B8R8_2PLANE_420_UNORM  },
    { DRM_FORMAT_NV16,   VK_FORMAT_G8_B8R8_2PLANE_422_UNORM  },
    { DRM_FORMAT_YUV420, VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM },
    { DRM_FORMAT_YUV422, VK_FORMAT_G8_B8_R8_3PLANE_422_UNORM },
    { DRM_FORMAT_YUV444, VK_FORMAT_G8_B8_R8_3PLANE_444_UNORM },

    // 3PACK16 formats split the memory in three 16-bit words, so they have an
    // inverted channel order compared to DRM formats.
    { DRM_FORMAT_P010, VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16  },
    { DRM_FORMAT_P210, VK_FORMAT_G10X6_B10X6R10X6_2PLANE_422_UNORM_3PACK16  },
    { DRM_FORMAT_P012, VK_FORMAT_G12X4_B12X4R12X4_2PLANE_420_UNORM_3PACK16  },
    { DRM_FORMAT_P016, VK_FORMAT_G16_B16R16_2PLANE_420_UNORM                },
    { DRM_FORMAT_Q410, VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_444_UNORM_3PACK16 },
};
