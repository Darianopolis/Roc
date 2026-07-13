#pragma once

#include <core/object.hpp>
#include <core/types.hpp>
#include <core/fd.hpp>
#include <core/hash.hpp>
#include <core/containers.hpp>
#include <core/id.hpp>
#include <core/memory.hpp>

#include <core/exec.hpp>

#include "functions.hpp"

#define GPU_VALIDATION_COMPATIBILITY 1

// -----------------------------------------------------------------------------

struct Gpu;
struct GpuImage;
struct GpuBuffer;
struct GpuSampler;
struct GpuSyncobj;

enum class GpuImageUsage : u32;

// -----------------------------------------------------------------------------

DECLARE_TAGGED_INTEGER(GpuDescriptorId, u16);

struct GpuDescriptorIdAllocator
{
    std::vector<GpuDescriptorId> freelist;
    GpuDescriptorId last_id;
    GpuDescriptorId max_id;

    GpuDescriptorIdAllocator() = default;
    GpuDescriptorIdAllocator(u32 count);

    auto allocate() -> GpuDescriptorId;
    void free(GpuDescriptorId);
};

// -----------------------------------------------------------------------------

using GpuDrmFormat   = u32;
using GpuDrmModifier = u64;

// Additional flags required to uniquely identify a format when paired with a VkFormat
enum class GpuVulkanFormatFlag : u32
{
    // DRM FourCC codes have format variants to ignore alpha channels (E.g. XRGB|ARGB).
    // Vulkan handles these in image view channel swizzles, instead of formats.
    ignore_alpha = 1 << 0,
};

struct GpuFormatInfo
{
    std::string_view name;

    GpuDrmFormat drm;

    VkFormat vk;
    VkFormat vk_srgb;
    Flags<GpuVulkanFormatFlag> vk_flags;

    bool is_ycbcr;

    u32 texel_block_size; // bytes
    u32 texels_per_block;
    VkExtent3D block_extent;
};

auto gpu_get_format_infos() -> std::span<const GpuFormatInfo>;

struct GpuFormat
{
    // Formats are stored as indices into the static format_infos table.
    u8 index;

    constexpr auto operator==(const GpuFormat&) const noexcept -> bool = default;

    constexpr explicit operator bool() const noexcept { return index; }

    constexpr const GpuFormatInfo* operator->() const noexcept
    {
        return &gpu_get_format_infos()[index];
    }
};

MAKE_STRUCT_HASHABLE(GpuFormat, v.index);

inline
auto gpu_get_formats()
{
    return std::views::iota(0)
         | std::views::take(gpu_get_format_infos().size())
         | std::views::transform([](usz i) { return GpuFormat(i); });
}

auto gpu_format_from_drm(GpuDrmFormat) -> GpuFormat;
auto gpu_format_from_vulkan(VkFormat, Flags<GpuVulkanFormatFlag> = {}) -> GpuFormat;

struct GpuFormatModifierProperties
{
    GpuDrmModifier modifier;
    VkFormatFeatureFlags2 features;
    u32 plane_count;

    VkExternalMemoryProperties ext_mem_props;

    vec2u32 max_extent;
    bool has_mutable_srgb;
};

using GpuFormatModifierSet = std::flat_set<GpuDrmModifier>;
inline const GpuFormatModifierSet gpu_empty_modifier_set;

struct GpuFormatProperties
{
    std::unique_ptr<GpuFormatModifierProperties> opt_props;
    std::vector<GpuFormatModifierProperties> mod_props;
    GpuFormatModifierSet mods;

    auto for_mod(GpuDrmModifier mod) const -> const GpuFormatModifierProperties*
    {
        for (auto& p : mod_props) {
            if (p.modifier == mod) return &p;
        }
        return nullptr;
    }
};

struct GpuFormatPropertiesKey
{
    VkFormat          format;
    VkImageUsageFlags usage;

    constexpr auto operator==(const GpuFormatPropertiesKey&) const noexcept -> bool = default;
};
MAKE_STRUCT_HASHABLE(GpuFormatPropertiesKey, v.format, v.usage);

struct GpuFormatSet
{
    ankerl::unordered_dense::map<GpuFormat, GpuFormatModifierSet> entries;

    void add(GpuFormat format, GpuDrmModifier modifier)
    {
        entries[format].insert(modifier);
    }

    void clear() { entries.clear(); }

    auto get(GpuFormat format) const noexcept -> const GpuFormatModifierSet&
    {
        auto iter = entries.find(format);
        return iter == entries.end() ? gpu_empty_modifier_set : iter->second;
    }

    auto  size() const -> usz  { return entries.size(); }
    auto empty() const -> bool { return !entries.empty(); }

    auto begin() const { return entries.begin(); }
    auto   end() const { return entries.end(); }
};

auto gpu_intersect_format_modifiers(std::span<const GpuFormatModifierSet* const> sets) -> GpuFormatModifierSet;
auto gpu_intersect_format_sets(std::span<const GpuFormatSet* const> sets) -> GpuFormatSet;

auto gpu_get_format_properties(Gpu*, GpuFormat, Flags<GpuImageUsage>) -> const GpuFormatProperties*;

auto gpu_get_modifier_name(GpuDrmModifier) -> std::string;

// -----------------------------------------------------------------------------

enum class GpuFeature : u32
{
    validation = 1 << 0,
    timelines  = 1 << 1,
};

struct Gpu
{
    GpuVulkanFunctions vk;

    Flags<GpuFeature> features;

    ExecContext* exec;

    void* loader;

    RENDERDOC_API_1_7_0* renderdoc;

    VkInstance instance;
    VkDebugUtilsMessengerEXT debug_messenger;
    VkPhysicalDevice physical_device;
    VkDevice device;

    ankerl::unordered_dense::set<Weak<void>> unbarriered_reads;
    ankerl::unordered_dense::set<Weak<void>> unbarriered_writes;

    struct {
        drmDevice* device;
        dev_t      id;
        fd_t       fd;

        u32 syncobj;
    } drm;

    VmaAllocator vma;

    struct {
        u32 active_images;
        usz active_image_memory;

        u32 active_buffers;
        usz active_buffer_memory;

        u32 active_samplers;

        u32 active_syncobjs;
    } stats;

    std::vector<VkSemaphore> free_binary_semaphores;

    VkDescriptorSetLayout set_layout;
    VkPipelineLayout pipeline_layout;
    VkDescriptorPool pool;
    VkDescriptorSet set;

    GpuDescriptorIdAllocator image_descriptor_allocator;
    GpuDescriptorIdAllocator sampler_descriptor_allocator;

    ankerl::unordered_dense::segmented_map<GpuFormatPropertiesKey, GpuFormatProperties> format_props;

    struct {
        u32 family;
        VkQueue queue;
        VkCommandPool pool;
        Ref<struct GpuCommands> commands;
        Ref<GpuSyncobj> syncobj;
        u64 submitted;
    } queue;

    ~Gpu();
};

auto gpu_create(ExecContext*, Flags<GpuFeature>) -> Ref<Gpu>;

// -----------------------------------------------------------------------------

struct GpuWaitFn
{
    Link<GpuWaitFn> link;

    u64 point;

    virtual void handle(u64 point) = 0;
    virtual ~GpuWaitFn() = default;
};

struct GpuSyncobj
{
    Gpu* gpu;

    VkSemaphore semaphore;
    u32 syncobj;

    struct {
        Fd  fd;
        u64 skips = 0;
        Link<GpuWaitFn> list;
    } wait;

    ~GpuSyncobj();
};

struct GpuSyncpoint
{
    GpuSyncobj*           syncobj;
    u64                   value = 0;
    VkPipelineStageFlags2 stages = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
};

auto gpu_syncobj_create(Gpu*) -> Ref<GpuSyncobj>;
auto gpu_syncobj_import(Gpu*, fd_t syncobj_fd) -> Ref<GpuSyncobj>;
auto gpu_syncobj_export(GpuSyncobj*) -> Fd;

void gpu_syncobj_import_syncfile(GpuSyncobj*, u64 target_point, fd_t sync_fd);
auto gpu_syncobj_export_syncfile(GpuSyncobj*, u64 source_point) -> Fd;

auto gpu_syncobj_get_value(   GpuSyncobj*) -> u64;
void gpu_syncobj_signal_value(GpuSyncobj*, u64 value);

void gpu_syncobj_wait(GpuSyncobj*, GpuWaitFn*);

void gpu_wait_blocking(GpuSyncpoint);

template<typename Fn>
void gpu_wait(GpuSyncpoint sync, Fn&& fn)
{
    struct Wait : GpuWaitFn
    {
        Fn fn;
        Wait(Fn&& fn): fn(std::move(fn)) {}
        virtual void handle(u64 value) final override { fn(value); }
    };
    auto wait = new Wait(std::move(fn));
    wait->point = sync.value;
    gpu_syncobj_wait(sync.syncobj, wait);
}

// -----------------------------------------------------------------------------

auto gpu_flush(Gpu*) -> GpuSyncpoint;

// -----------------------------------------------------------------------------

struct GpuBuffer
{
    Gpu* gpu;

    VkBuffer buffer;
    VmaAllocation vma_allocation;
    VkDeviceAddress device_address;
    void* host_address;
    usz size;

    template<typename T>
    T* device(usz byte_offset = 0) const
    {
        return reinterpret_cast<T*>(device_address + byte_offset);
    }

    template<typename T>
    T* host(usz byte_offset = 0) const
    {
        return byte_offset_pointer<T>(host_address, byte_offset);
    }

    ~GpuBuffer();
};

enum class GpuBufferFlag : u32
{
    host = 1 << 0,
};

auto gpu_buffer_create(Gpu*, usz size, Flags<GpuBufferFlag>) -> Ref<GpuBuffer>;

// -----------------------------------------------------------------------------

enum class GpuImageUsage : u32
{
    transfer_src = 1 << 0,
    transfer_dst = 1 << 1,
    transfer     = transfer_dst | transfer_src,
    texture      = 1 << 2,
    render       = 1 << 3,
    storage      = 1 << 4,
};

struct GpuImageBase;

struct GpuImage
{
    virtual ~GpuImage() = default;

    virtual auto base() -> GpuImageBase* = 0;
};

struct GpuImageBase : GpuImage
{
    Gpu* gpu;

    GpuFormat format;
    GpuDrmModifier modifier = DRM_FORMAT_MOD_INVALID;

    VkImage     image;
    VkImageView view;
    vec2u32     extent;

    GpuDescriptorId id;

    Flags<GpuImageUsage> usage;

    virtual ~GpuImageBase();

    virtual auto base() -> GpuImageBase* final override { return this; }
};

enum class GpuImageFlag
{
    host = 1 << 0,
};

struct GpuImageCreateInfo
{
    vec2u32                     extent;
    GpuFormat                   format;
    Flags<GpuImageUsage>        usage;
    Flags<GpuImageFlag>         flags;
    const GpuFormatModifierSet* modifiers;
};

auto gpu_image_create(Gpu*, const GpuImageCreateInfo&) -> Ref<GpuImage>;

void gpu_copy_image_to_buffer(GpuBuffer*, GpuImage*);

struct GpuBufferImageCopy
{
    vec2u32 image_extent;
    vec2i32 image_offset;
    u32 buffer_offset;
    u32 buffer_row_length;
};

void gpu_copy_buffer_to_image(GpuImage*, GpuBuffer*, std::span<const GpuBufferImageCopy> regions);

void gpu_copy_memory_to_image(GpuImage*, std::span<const byte> data, std::span<const GpuBufferImageCopy> regions);

auto gpu_image_compute_packed_stride(GpuFormat, u32 width) -> u32;
auto gpu_image_compute_linear_offset(GpuFormat, vec2u32 position, u32 row_stride_bytes) -> u32;

// -----------------------------------------------------------------------------

struct GpuSampler
{
    Gpu* gpu;

    VkSampler sampler;

    GpuDescriptorId id;

    ~GpuSampler();
};

struct GpuSamplerCreateInfo
{
    VkFilter mag;
    VkFilter min;
};

auto gpu_sampler_create(Gpu*, const GpuSamplerCreateInfo&) -> Ref<GpuSampler>;

// -----------------------------------------------------------------------------

enum class GpuBlendMode : u32
{
    none,
    premultiplied,
    postmultiplied,
};

struct GpuShaderStageInfo
{
    VkShaderStageFlagBits stage;
    std::span<const u32> code;
    const char* entry;
};

struct GpuPipeline;

struct GpuGraphicsPipelineCreateInfo
{
    GpuFormat format;
    std::span<const GpuShaderStageInfo> shaders;
    VkPrimitiveTopology topology;
    GpuBlendMode blend_mode;
};

auto gpu_pipeline_create(Gpu*, const GpuGraphicsPipelineCreateInfo&) -> Ref<GpuPipeline>;

auto gpu_pipeline_create_compute(Gpu*, const GpuShaderStageInfo&) -> Ref<GpuPipeline>;

void gpu_dispatch(Gpu*, GpuPipeline*, vec3u32, std::span<const byte> push_data);

// -----------------------------------------------------------------------------

enum class GpuDepthEnable
{
    test  = 1 << 0,
    write = 1 << 1,
};

struct GpuDrawInfo
{
    u32 index_count;
    u32 instance_count;
    u32 first_index;
    u32 vertex_offset;
    u32 first_instance;
};

struct GpuRenderPass;

void gpu_push_constants(   GpuRenderPass*, u32 offset, std::span<const byte> data);
void gpu_set_scissors(     GpuRenderPass*, std::span<const rect2i32> scissors);
void gpu_set_viewports(    GpuRenderPass*, std::span<const rect2f32> viewports);
void gpu_bind_index_buffer(GpuRenderPass*, GpuBuffer*, u32 offset, VkIndexType);
void gpu_bind_pipeline(    GpuRenderPass*, GpuPipeline*);
void gpu_draw_indexed(     GpuRenderPass*, const GpuDrawInfo&);

struct GpuRenderPassInfo
{
    GpuImage* target;
    std::optional<vec4f32> clear_color;
    const ankerl::unordered_dense::set<void*>* reads;
    const ankerl::unordered_dense::set<void*>* writes;
};

void gpu_render(Gpu*, const GpuRenderPassInfo&, std::function_ref<void(GpuRenderPass*)>);

// -----------------------------------------------------------------------------

constexpr static u32 gpu_dma_max_planes = 4;

struct GpuDmaPlane
{
    Fd  fd;
    u32 offset;
    u32 stride;
};

struct GpuDmaParams
{
    FixedArray<GpuDmaPlane, gpu_dma_max_planes> planes;
    bool disjoint;

    vec2u32        extent;
    GpuFormat      format;
    GpuDrmModifier modifier;
};

auto gpu_image_import(Gpu*, const GpuDmaParams&, Flags<GpuImageUsage>) -> Ref<GpuImage>;
auto gpu_image_is_exportable(GpuImage*) -> bool;
auto gpu_image_export(GpuImage*) -> GpuDmaParams;

// -----------------------------------------------------------------------------

struct GpuImageHandle
{
    GpuDescriptorId image;
    GpuDescriptorId sampler;

    GpuImageHandle() = default;

    GpuImageHandle(GpuImage* image, GpuSampler* sampler)
        : image  (image->base()->id)
        , sampler(sampler ? sampler->id : GpuDescriptorId{})
    {}
};

// -----------------------------------------------------------------------------

template<typename Release>
struct GpuImageLease : GpuImage
{
    Ref<GpuImage> image;
    Release       release;

    GpuImageLease(Ref<GpuImage> image, Release&& lessor)
        : image(std::move(image))
        , release(std::move(lessor))
    {}

    virtual auto base() -> GpuImageBase* final override { return image->base(); }

    ~GpuImageLease()
    {
        release(std::move(image));
    }
};

template<typename Release>
auto gpu_lease_image(Ref<GpuImage> image, Release&& release) -> Ref<GpuImageLease<Release>>
{
    return ref_create<GpuImageLease<Release>>(std::move(image), std::move(release));
}

// -----------------------------------------------------------------------------

struct GpuImagePool
{
    virtual ~GpuImagePool() = default;

    virtual auto acquire(const GpuImageCreateInfo&) -> Ref<GpuImage> = 0;
};

auto gpu_image_pool_create(Gpu*) -> Ref<GpuImagePool>;
