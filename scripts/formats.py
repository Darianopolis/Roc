from xml.etree import ElementTree
from enum import Flag, auto
from .utils import write_file_lazy, ensure_dir

class VkFlags(Flag):
    ignore_alpha = auto()

def format(drm: str, vk: str, flags: VkFlags = VkFlags(0)):
    return (drm, vk, flags)

formats = [
    format("    ", "VK_FORMAT_UNDEFINED"),

    # Vulkan non-packed 8-bits-per-channel formats have an inverted channel
    # order compared to the DRM formats, because DRM format channel order
    # is little-endian while Vulkan format channel order is in memory byte
    # order.
    format("R8  ", "VK_FORMAT_R8_UNORM",                           ),
    format("R  H", "VK_FORMAT_R16_SFLOAT"                          ),
    format("R  F", "VK_FORMAT_R32_SFLOAT"                          ),
    format("GR88", "VK_FORMAT_R8G8_UNORM"                          ),
    format("GR H", "VK_FORMAT_R16G16_SFLOAT"                       ),
    format("GR F", "VK_FORMAT_R32G32_SFLOAT"                       ),
    format("RG24", "VK_FORMAT_B8G8R8_UNORM"                        ),
    format("BG24", "VK_FORMAT_R8G8B8_UNORM"                        ),
    format("AR24", "VK_FORMAT_B8G8R8A8_UNORM"                      ),
    format("XR24", "VK_FORMAT_B8G8R8A8_UNORM", VkFlags.ignore_alpha),
    format("AB24", "VK_FORMAT_R8G8B8A8_UNORM"                      ),
    format("XB24", "VK_FORMAT_R8G8B8A8_UNORM", VkFlags.ignore_alpha),

    # Vulkan packed formats have the same channel order as DRM formats on
    # little endian systems.
    format("RA12", "VK_FORMAT_R4G4B4A4_UNORM_PACK16"                         ),
    format("RX12", "VK_FORMAT_R4G4B4A4_UNORM_PACK16",    VkFlags.ignore_alpha),
    format("BA12", "VK_FORMAT_B4G4R4A4_UNORM_PACK16"                         ),
    format("BX12", "VK_FORMAT_B4G4R4A4_UNORM_PACK16",    VkFlags.ignore_alpha),
    format("RG16", "VK_FORMAT_R5G6B5_UNORM_PACK16"                           ),
    format("BG16", "VK_FORMAT_B5G6R5_UNORM_PACK16"                           ),
    format("RA15", "VK_FORMAT_R5G5B5A1_UNORM_PACK16"                         ),
    format("RX15", "VK_FORMAT_R5G5B5A1_UNORM_PACK16",    VkFlags.ignore_alpha),
    format("BA15", "VK_FORMAT_B5G5R5A1_UNORM_PACK16"                         ),
    format("BX15", "VK_FORMAT_B5G5R5A1_UNORM_PACK16",    VkFlags.ignore_alpha),
    format("AR15", "VK_FORMAT_A1R5G5B5_UNORM_PACK16"                         ),
    format("XR15", "VK_FORMAT_A1R5G5B5_UNORM_PACK16",    VkFlags.ignore_alpha),
    format("AR30", "VK_FORMAT_A2R10G10B10_UNORM_PACK32"                      ),
    format("XR30", "VK_FORMAT_A2R10G10B10_UNORM_PACK32", VkFlags.ignore_alpha),
    format("AB30", "VK_FORMAT_A2B10G10R10_UNORM_PACK32"                      ),
    format("XB30", "VK_FORMAT_A2B10G10R10_UNORM_PACK32", VkFlags.ignore_alpha),

    # Vulkan 16-bits-per-channel formats have an inverted channel order
    # compared to DRM formats, just like the 8-bits-per-channel ones.
    # On little endian systems the memory representation of each channel
    # matches the DRM formats'.
    format("BG48", "VK_FORMAT_R16G16B16_UNORM"                          ),
    format("BGRH", "VK_FORMAT_R16G16B16_SFLOAT"                         ),
    format("AB48", "VK_FORMAT_R16G16B16A16_UNORM"                       ),
    format("XB48", "VK_FORMAT_R16G16B16A16_UNORM",  VkFlags.ignore_alpha),
    format("AB4H", "VK_FORMAT_R16G16B16A16_SFLOAT"                      ),
    format("XB4H", "VK_FORMAT_R16G16B16A16_SFLOAT", VkFlags.ignore_alpha),
    format("BGRF", "VK_FORMAT_R32G32B32_SFLOAT"                         ),
    format("AB8F", "VK_FORMAT_R32G32B32A32_SFLOAT"                      ),

    # YCbCr formats
    # R -> V, G -> Y, B -> U
    # 420 -> 2x2 subsampled, 422 -> 2x1 subsampled, 444 -> non-subsampled
    format("UYVY", "VK_FORMAT_B8G8R8G8_422_UNORM"       ),
    format("YUYV", "VK_FORMAT_G8B8G8R8_422_UNORM"       ),
    format("NV12", "VK_FORMAT_G8_B8R8_2PLANE_420_UNORM" ),
    format("NV16", "VK_FORMAT_G8_B8R8_2PLANE_422_UNORM" ),
    format("YU12", "VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM"),
    format("YU16", "VK_FORMAT_G8_B8_R8_3PLANE_422_UNORM"),
    format("YU24", "VK_FORMAT_G8_B8_R8_3PLANE_444_UNORM"),

    # 3PACK16 formats split the memory in three 16-bit words, so they have an
    # inverted channel order compared to DRM formats.
    format("P010", "VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16" ),
    format("P210", "VK_FORMAT_G10X6_B10X6R10X6_2PLANE_422_UNORM_3PACK16" ),
    format("P012", "VK_FORMAT_G12X4_B12X4R12X4_2PLANE_420_UNORM_3PACK16" ),
    format("P016", "VK_FORMAT_G16_B16R16_2PLANE_420_UNORM"               ),
    format("Q410", "VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_444_UNORM_3PACK16"),
]

def enumerate_vk_formats(path: str):
    entries = {}

    tree = ElementTree.parse(path)
    root = tree.getroot()
    formats = root.find("formats")
    if formats:
        for format in formats.findall("format"):
            name = format.get("name", "")
            entries[name] = format

    return entries

def gen_flags(flags: VkFlags):
    if not flags:
        return "Flags<GpuVulkanFormatFlag>{}"
    return "|".join([f"GpuVulkanFormatFlag::{str(flag)[8:]}" for flag in flags])

def generate_formats(build_dir):
    format_entries = enumerate_vk_formats(build_dir / "vendor/vulkan-headers/registry/vk.xml")

    def get_srgb_variant(vk):
        if vk.endswith("_UNORM"):
            vk_srgb = vk[0:-6] + "_SRGB"
            if vk_srgb in format_entries:
                return vk_srgb
        return None

    out = ""
    out +=  "#include <gpu/internal.hpp>\n"
    out += "\n"

    # Format list

    out +=  "static\n"
    out += f"constexpr std::array<GpuFormatInfo, {len(formats) + 1}> gpu_format_infos =\n"
    out +=  "{\n"
    for (drm, vk, flags) in formats:
        out +=  "    GpuFormatInfo {\n"

        # DRM

        out += f'        .name = "{drm}",\n'
        out += f"        .drm = fourcc_code('{drm[0]}', '{drm[1]}', '{drm[2]}', '{drm[3]}'),\n"

        # Vulkan

        out += f"        .vk = {vk},\n"
        vk_srgb = get_srgb_variant(vk)
        if vk_srgb:
            out += f"        .vk_srgb = {vk_srgb},\n"
        if flags:
            out += f"        .vk_flags = {gen_flags(flags)},\n"

        entry = format_entries.get(vk)
        if entry:
            if entry.get("chroma", ""):
                out += "        .is_ycbcr = true,\n"

            # Block / Texel dimensions

            out += f"        .texel_block_size = {int(entry.get("blockSize", "0"))},\n"
            out += f"        .texels_per_block = {int(entry.get("texelsPerBlock", "1"))},\n"

            raw_extent = entry.get("blockExtent", "1,1,1")
            bx, by, bz = (int(v) for v in raw_extent.split(","))
            out += f"        .block_extent = {{{bx},{by},{bz}}},\n"

        out += "    },\n"
    out += "};\n"
    out += "\n"
    out += "auto gpu_get_format_infos() -> std::span<const GpuFormatInfo>\n"
    out += "{\n"
    out += "    return gpu_format_infos;\n"
    out += "}\n"

    # DRM FourCC -> GpuFormat

    out += "auto gpu_format_from_drm(GpuDrmFormat drm_format) -> GpuFormat\n"
    out += "{\n"
    out += "    switch (drm_format) {\n"
    for i, (drm, vk, flags) in enumerate(formats):
        out += f"        break;case fourcc_code('{drm[0]}', '{drm[1]}', '{drm[2]}', '{drm[3]}'): return {{{i}}};\n"
    out += "    }\n"
    out += "    return {};\n"
    out += "}\n"
    out += "\n"

    # VkFormat -> GpuFormat

    out += "auto gpu_format_from_vulkan(VkFormat vk_format, Flags<GpuVulkanFormatFlag> vk_flags) -> GpuFormat\n"
    out += "{\n"
    out += "    switch (vk_format) {\n"
    from_vulkan = {}
    for i, (drm, vk, flags) in enumerate(formats):
        if vk not in from_vulkan:
            from_vulkan[vk] = []
        from_vulkan[vk].append((i, flags))
    for vk, variants in from_vulkan.items():
        def gen_case(name):
            out = ""
            out += f"        break;case {name}:\n"
            for (i, flags) in variants:
                out += f"            if (vk_flags == {gen_flags(flags)}) return {{{i}}};\n"
            return out
        out += gen_case(vk)
        vk_srgb = get_srgb_variant(vk)
        if vk_srgb:
            out += gen_case(vk_srgb)
    out += "        break;default:\n"
    out += "            ;\n"
    out += "    }\n"
    out += "    return {};\n"
    out += "}\n"

    format_dir = ensure_dir(build_dir / "formats")
    write_file_lazy(format_dir / "formats.cpp", out)
