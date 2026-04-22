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
    std::string name;

    bool is_ycbcr;

    GpuDrmFormat drm;

    VkFormat vk;
    VkFormat vk_srgb;
    Flags<GpuVulkanFormatFlag> vk_flags;

    VKU_FORMAT_INFO info;
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
auto gpu_format_from_vk(VkFormat, Flags<GpuVulkanFormatFlag> = {}) -> GpuFormat;

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

    struct {
        drmDevice* device;
        dev_t      id;
        int        fd;

        u32 syncobj;
    } drm;

    VmaAllocator vma;

    struct {
        u32 active_images;
        usz active_image_memory;

        u32 active_buffers;
        usz active_buffer_memory;

        u32 active_samplers;
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

struct GpuWaitFn : IntrusiveListBase<GpuWaitFn>
{
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
        IntrusiveList<GpuWaitFn> list;
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
auto gpu_syncobj_import(Gpu*, int syncobj_fd) -> Ref<GpuSyncobj>;
auto gpu_syncobj_export(GpuSyncobj*) -> Fd;

void gpu_syncobj_import_syncfile(GpuSyncobj*, u64 target_point, int sync_fd);
auto gpu_syncobj_export_syncfile(GpuSyncobj*, u64 source_point) -> Fd;

auto gpu_syncobj_get_value(   GpuSyncobj*) -> u64;
void gpu_syncobj_signal_value(GpuSyncobj*, u64 value);

void gpu_syncobj_wait(GpuSyncobj*, GpuWaitFn*);

// WARNING: Blocking
void gpu_wait(GpuSyncpoint);

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

void gpu_protect(Gpu*, Ref<void>);

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

template<typename T>
struct GpuArrayElementProxy
{
    T* host_value;

    void operator=(const T& value)
    {
        std::memcpy(host_value, &value, sizeof(value));
    }
};

template<typename T>
struct GpuArray
{
    Ref<GpuBuffer> buffer;
    usz count = 0;
    usz byte_offset = 0;

    GpuArray() = default;

    GpuArray(const auto& buffer, usz count, usz byte_offset = {})
        : buffer(buffer)
        , count(count)
        , byte_offset(byte_offset)
    {}

    auto device() const -> T*
    {
        return buffer->device<T>(byte_offset);
    }

    auto host() const -> T*
    {
        return buffer->host<T>(byte_offset);
    }

    auto operator[](usz index) const -> GpuArrayElementProxy<T>
    {
        return {buffer->host<T>(byte_offset) + index};
    }
};

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

struct GpuImage
{
    virtual ~GpuImage() = default;

    virtual auto base() -> GpuImage* = 0;

    auto context()    -> Gpu*;
    auto extent()     -> vec2u32;
    auto format()     -> GpuFormat;
    auto modifier()   -> GpuDrmModifier;
    auto view()       -> VkImageView;
    auto handle()     -> VkImage;
    auto usage()      -> Flags<GpuImageUsage>;
    auto descriptor() -> GpuDescriptorId;
};

struct GpuImageCreateInfo
{
    vec2u32                     extent;
    GpuFormat                   format;
    Flags<GpuImageUsage>        usage;
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

auto gpu_image_compute_linear_offset(GpuFormat, vec2u32 position, u32 stride) -> u32;

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

struct GpuShader;

struct GpuShaderCreateInfo
{
    VkShaderStageFlagBits stage;
    std::span<const u32>  code;
    const char*           entry;
};

auto gpu_shader_create(Gpu*, const GpuShaderCreateInfo&) -> Ref<GpuShader>;

// -----------------------------------------------------------------------------

enum class GpuDepthEnable
{
    test  = 1 << 0,
    write = 1 << 1,
};

struct GpuDrawInfo {
    u32 index_count;
    u32 instance_count;
    u32 first_index;
    u32 vertex_offset;
    u32 first_instance;
};

struct GpuRenderpass
{
    Gpu* gpu;
    VkCommandBuffer cmd;

    void push_constants(   u32 offset, std::span<const byte> data);
    void set_scissors(     std::span<const rect2i32> scissors);
    void set_viewports(    std::span<const rect2f32> viewports);
    void set_polygon_state(VkPrimitiveTopology, VkPolygonMode, f32 line_width);
    void set_cull_state(   VkCullModeFlagBits, VkFrontFace);
    void set_depth_state(  Flags<GpuDepthEnable> enabled, VkCompareOp);
    void set_blend_state(  std::span<const GpuBlendMode>);
    void bind_index_buffer(GpuBuffer*, u32 offset, VkIndexType);
    void bind_shaders(     std::span<GpuShader* const>);
    void draw_indexed(     const GpuDrawInfo&);
};

struct GpuRenderpassInfo
{
    GpuImage* target;
    vec4f32   clear_color;
};

auto gpu_renderpass_begin(Gpu*, const GpuRenderpassInfo&) -> GpuRenderpass;
void gpu_renderpass_end(GpuRenderpass&);

template<typename Fn>
void gpu_render(Gpu* gpu, const GpuRenderpassInfo& info, Fn&& fn)
{
    auto pass = gpu_renderpass_begin(gpu, info);
    fn(pass);
    gpu_renderpass_end(pass);
}

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
auto gpu_image_export(GpuImage*) -> GpuDmaParams;

// -----------------------------------------------------------------------------

struct GpuImageHandle
{
    GpuDescriptorId image;
    GpuDescriptorId sampler;

    GpuImageHandle() = default;

    GpuImageHandle(GpuImage* image, GpuSampler* sampler)
        : image  (image->descriptor())
        , sampler(sampler ? sampler->id : GpuDescriptorId{})
    {}
};

// -----------------------------------------------------------------------------

template<typename Lessor>
struct GpuImageLease : GpuImage
{
    Ref<GpuImage> image;
    Lessor        lessor;

    GpuImageLease(Ref<GpuImage> image, Lessor&& lessor)
        : image(std::move(image))
        , lessor(std::move(lessor))
    {}

    virtual auto base() -> GpuImage* final override { return image->base(); }

    ~GpuImageLease()
    {
        lessor(std::move(image));
    }
};

template<typename Lessor>
auto gpu_lease_image(Ref<GpuImage> image, Lessor&& lessor) -> Ref<GpuImageLease<Lessor>>
{
    return ref_create<GpuImageLease<Lessor>>(std::move(image), std::move(lessor));
}

// -----------------------------------------------------------------------------

struct GpuImagePool
{
    virtual ~GpuImagePool() = default;

    virtual auto acquire(const GpuImageCreateInfo&) -> Ref<GpuImage> = 0;
};

auto gpu_image_pool_create(Gpu*) -> Ref<GpuImagePool>;
