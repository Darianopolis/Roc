#pragma once

#include "core/object.hpp"
#include "core/types.hpp"
#include "core/event.hpp"
#include "core/fd.hpp"
#include "core/hash.hpp"
#include "core/containers.hpp"
#include "core/memory.hpp"

#include "functions.hpp"

// -----------------------------------------------------------------------------

struct gpu_image;
struct gpu_buffer;
struct gpu_sampler;
struct gpu_semaphore;
struct gpu_commands;
struct gpu_queue;

enum class gpu_image_usage : u32;

// -----------------------------------------------------------------------------

enum class gpu_descriptor_id : u32 { invalid = 0 };

struct gpu_descriptor_id_allocator
{
    std::vector<gpu_descriptor_id> freelist;
    u32 next_id;
    u32 capacity;

    gpu_descriptor_id_allocator() = default;
    gpu_descriptor_id_allocator(u32 count);

    auto allocate() -> gpu_descriptor_id;
    void free(gpu_descriptor_id);
};

// -----------------------------------------------------------------------------

using gpu_drm_format   = u32;
using gpu_drm_modifier = u64;

// Additional flags required to uniquely identify a format when paired with a VkFormat
enum class gpu_vk_format_flag : u32
{
    // DRM FourCC codes have format variants to ignore alpha channels (E.g. XRGB|ARGB).
    // Vulkan handles these in image view channel swizzles, instead of formats.
    ignore_alpha = 1 << 0,
};

struct gpu_format_info
{
    std::string name;

    bool is_ycbcr;

    gpu_drm_format drm;

    VkFormat vk;
    VkFormat vk_srgb;
    flags<gpu_vk_format_flag> vk_flags;

    VKU_FORMAT_INFO info;
};

auto gpu_get_format_infos() -> std::span<const gpu_format_info>;

struct gpu_format
{
    // Formats are stored as indices into the static format_infos table.
    u8 index;

    constexpr bool operator==(const gpu_format&) const noexcept = default;

    constexpr explicit operator bool() const noexcept { return index; }

    constexpr const gpu_format_info* operator->() const noexcept
    {
        return &gpu_get_format_infos()[index];
    }
};

CORE_MAKE_STRUCT_HASHABLE(gpu_format, v.index);

inline
auto gpu_get_formats()
{
    return std::views::iota(0)
         | std::views::take(gpu_get_format_infos().size())
         | std::views::transform([](usz i) { return gpu_format(i); });
}

auto gpu_format_from_drm(gpu_drm_format) -> gpu_format;
auto gpu_format_from_vk(VkFormat, flags<gpu_vk_format_flag> = {}) -> gpu_format;

struct gpu_format_modifier_props
{
    gpu_drm_modifier modifier;
    VkFormatFeatureFlags2 features;
    u32 plane_count;

    VkExternalMemoryProperties ext_mem_props;

    vec2u32 max_extent;
    bool has_mutable_srgb;
};

using gpu_format_modifier_set = std::flat_set<gpu_drm_modifier>;
inline const gpu_format_modifier_set gpu_empty_modifier_set;

struct gpu_format_props
{
    std::unique_ptr<gpu_format_modifier_props> opt_props;
    std::vector<gpu_format_modifier_props> mod_props;
    gpu_format_modifier_set mods;

    auto for_mod(gpu_drm_modifier mod) const -> const gpu_format_modifier_props*
    {
        for (auto& p : mod_props) {
            if (p.modifier == mod) return &p;
        }
        return nullptr;
    }
};

struct gpu_format_props_key
{
    VkFormat format;
    VkImageUsageFlags usage;

    constexpr bool operator==(const gpu_format_props_key&) const noexcept = default;
};
CORE_MAKE_STRUCT_HASHABLE(gpu_format_props_key, v.format, v.usage);


struct gpu_format_set
{
    ankerl::unordered_dense::map<gpu_format, gpu_format_modifier_set> entries;

    void add(gpu_format format, gpu_drm_modifier modifier)
    {
        entries[format].insert(modifier);
    }

    void clear() { entries.clear(); }

    auto get(gpu_format format) const noexcept -> const gpu_format_modifier_set&
    {
        auto iter = entries.find(format);
        return iter == entries.end() ? gpu_empty_modifier_set : iter->second;
    }

    usz   size() const { return entries.size(); }
    bool empty() const { return !entries.empty(); }

    auto begin() const { return entries.begin(); }
    auto   end() const { return entries.end(); }
};

auto gpu_intersect_format_modifiers(std::span<const gpu_format_modifier_set* const> sets) -> gpu_format_modifier_set;
auto gpu_intersect_format_sets(std::span<const gpu_format_set* const> sets) -> gpu_format_set;

auto gpu_get_format_properties(gpu_context*, gpu_format, flags<gpu_image_usage>) -> const gpu_format_props*;

auto gpu_get_modifier_name(gpu_drm_modifier) -> std::string;

// -----------------------------------------------------------------------------

enum class gpu_feature : u32
{
    validation = 1 << 0,
};

struct gpu_context
{
    flags<gpu_feature> features;

    struct {
        GPU_DECLARE_FUNCTION(GetInstanceProcAddr)
        GPU_DECLARE_FUNCTION(CreateInstance)
        GPU_INSTANCE_FUNCTIONS(GPU_DECLARE_FUNCTION)
        GPU_DEVICE_FUNCTIONS(  GPU_DECLARE_FUNCTION)
    } vk;

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
    } drm;

    VmaAllocator vma;

    struct {
        u32 active_images;
        usz active_image_memory;

        u32 active_buffers;
        usz active_buffer_memory;

        u32 active_samplers;
    } stats;

    ref<gpu_queue> graphics_queue;
    ref<gpu_queue> transfer_queue;
    ref<core_event_loop> event_loop;

    std::vector<VkSemaphore> free_binary_semaphores;

    VkDescriptorSetLayout set_layout;
    VkPipelineLayout pipeline_layout;
    VkDescriptorPool pool;
    VkDescriptorSet set;

    gpu_descriptor_id_allocator image_descriptor_allocator;
    gpu_descriptor_id_allocator sampler_descriptor_allocator;

    ankerl::unordered_dense::segmented_map<gpu_format_props_key, gpu_format_props> format_props;

    ~gpu_context();
};

auto gpu_create(flags<gpu_feature>, core_event_loop*) -> ref<gpu_context>;

// -----------------------------------------------------------------------------

enum class gpu_queue_type : u32
{
    graphics,
    transfer,
};

struct gpu_queue
{
    gpu_context* gpu;

    gpu_queue_type type;
    u32 family;
    VkQueue queue;

    VkCommandPool cmd_pool;
    VkCommandBuffer cmd;

    ref<gpu_semaphore> queue_sema;

    u64 submitted;

    ~gpu_queue();
};

auto gpu_get_queue(gpu_context*, gpu_queue_type) -> gpu_queue*;

// -----------------------------------------------------------------------------

struct gpu_wait_fn : core_intrusive_list_base<gpu_wait_fn>
{
    u64 point;

    virtual void handle(u64 point) = 0;
    virtual ~gpu_wait_fn() = default;
};

struct gpu_semaphore
{
    gpu_context* gpu;

    VkSemaphore semaphore;
    u32         syncobj;

    struct {
        core_fd fd;
        u64 skips = 0;
        core_intrusive_list<gpu_wait_fn> list;
    } wait;

    ~gpu_semaphore();
};

struct gpu_syncpoint
{
    gpu_semaphore* semaphore;
    u64 value = 0;
    VkPipelineStageFlags2 stages = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
};

auto gpu_semaphore_create(gpu_context*) -> ref<gpu_semaphore>;
auto gpu_semaphore_import_syncobj(gpu_context*, int syncobj_fd) -> ref<gpu_semaphore>;
int  gpu_semaphore_export_syncobj(gpu_semaphore*);

void gpu_semaphore_import_syncfile(gpu_semaphore*, int sync_fd, u64 target_point);
int  gpu_semaphore_export_syncfile(gpu_semaphore*, u64 source_point);

u64  gpu_semaphore_get_value(   gpu_semaphore*);
void gpu_semaphore_signal_value(gpu_semaphore*, u64 value);

void gpu_semaphore_wait(gpu_semaphore*, gpu_wait_fn*);

// WARNING: Blocking
void gpu_wait(gpu_syncpoint);

template<typename Fn>
void gpu_wait(gpu_syncpoint sync, Fn&& fn)
{
    struct wait_item : gpu_wait_fn
    {
        Fn fn;
        wait_item(Fn&& fn): fn(std::move(fn)) {}
        virtual void handle(u64 value) final override { fn(value); }
    };
    auto wait = new wait_item(std::move(fn));
    wait->point = sync.value;
    gpu_semaphore_wait(sync.semaphore, wait);
}

// -----------------------------------------------------------------------------

struct gpu_commands
{
    gpu_queue* queue;

    VkCommandBuffer buffer;
    core_ref_vector<void> objects;

    u64 submitted_value;

    ~gpu_commands();
};

auto gpu_begin(gpu_queue*) -> ref<gpu_commands>;

void gpu_protect(gpu_commands*, ref<void>);

auto gpu_submit(gpu_commands*, std::span<const gpu_syncpoint> waits) -> gpu_syncpoint;

// WARNING: Blocking
void gpu_wait_idle(gpu_context*);

// WARNING: Blocking
void gpu_wait_idle(gpu_queue*);

// -----------------------------------------------------------------------------

struct gpu_buffer
{
    gpu_context* gpu;

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
        return core_byte_offset_pointer<T>(host_address, byte_offset);
    }

    ~gpu_buffer();
};

enum class gpu_buffer_flag : u32
{
    host = 1 << 0,
};

auto gpu_buffer_create(gpu_context*, usz size, flags<gpu_buffer_flag>) -> ref<gpu_buffer>;

// -----------------------------------------------------------------------------

template<typename T>
struct gpu_array_element_proxy
{
    T* host_value;

    void operator=(const T& value)
    {
        std::memcpy(host_value, &value, sizeof(value));
    }
};

template<typename T>
struct gpu_array
{
    ref<gpu_buffer> buffer;
    usz count = 0;
    usz byte_offset = 0;

    gpu_array() = default;

    gpu_array(const auto& buffer, usz count, usz byte_offset = {})
        : buffer(buffer)
        , count(count)
        , byte_offset(byte_offset)
    {}

    T* device() const
    {
        return buffer->device<T>(byte_offset);
    }

    T* host() const
    {
        return buffer->host<T>(byte_offset);
    }

    auto operator[](usz index) const -> gpu_array_element_proxy<T>
    {
        return {buffer->host<T>(byte_offset) + index};
    }
};

// -----------------------------------------------------------------------------

enum class gpu_image_usage : u32
{
    transfer_src = 1 << 0,
    transfer_dst = 1 << 1,
    transfer     = transfer_dst | transfer_src,
    texture      = 1 << 2,
    render       = 1 << 3,
    storage      = 1 << 4,
};

struct gpu_image
{
    virtual ~gpu_image() = default;

    virtual auto base() -> gpu_image* = 0;

    auto context()    -> gpu_context*;
    auto extent()     -> vec2u32;
    auto format()     -> gpu_format;
    auto modifier()   -> gpu_drm_modifier;
    auto view()       -> VkImageView;
    auto handle()     -> VkImage;
    auto usage()      -> flags<gpu_image_usage>;
    auto descriptor() -> gpu_descriptor_id;
};

struct gpu_image_create_info
{
    vec2u32                        extent;
    gpu_format                     format;
    flags<gpu_image_usage>         usage;
    const gpu_format_modifier_set* modifiers;
};

auto gpu_image_create(gpu_context*, const gpu_image_create_info&) -> ref<gpu_image>;

void gpu_cmd_copy_image_to_buffer(gpu_commands*, gpu_buffer*, gpu_image*);

struct gpu_buffer_image_copy
{
    vec2u32 image_extent;
    vec2i32 image_offset;
    u32 buffer_offset;
    u32 buffer_row_length;
};

void gpu_cmd_copy_buffer_to_image(gpu_commands*, gpu_image*, gpu_buffer*, std::span<const gpu_buffer_image_copy> regions);
void gpu_cmd_copy_memory_to_image(gpu_commands*, gpu_image*, core_byte_view data, std::span<const gpu_buffer_image_copy> regions);

// WARNING: Blocking
void gpu_copy_memory_to_image(gpu_image*, core_byte_view data, std::span<const gpu_buffer_image_copy> regions);

auto gpu_image_compute_linear_offset(gpu_format, vec2u32 position, u32 stride) -> u32;

// -----------------------------------------------------------------------------

struct gpu_sampler
{
    gpu_context* gpu;

    VkSampler sampler;

    gpu_descriptor_id id;

    ~gpu_sampler();
};

struct gpu_sampler_create_info
{
    VkFilter mag;
    VkFilter min;
};

auto gpu_sampler_create(gpu_context*, const gpu_sampler_create_info&) -> ref<gpu_sampler>;

// -----------------------------------------------------------------------------

enum class gpu_blend_mode : u32
{
    none,
    premultiplied,
    postmultiplied,
};

struct gpu_shader;

struct gpu_shader_create_info
{
    VkShaderStageFlagBits stage;
    std::span<const u32>  code;
    const char*           entry;
};

auto gpu_shader_create(gpu_context*, const gpu_shader_create_info&) -> ref<gpu_shader>;

enum class gpu_depth_enable
{
    test  = 1 << 0,
    write = 1 << 1,
};

void gpu_cmd_push_constants(   gpu_commands*, u32 offset, core_byte_view data);
void gpu_cmd_set_scissors(     gpu_commands*, std::span<const rect2i32> scissors);
void gpu_cmd_set_viewports(    gpu_commands*, std::span<const rect2f32> viewports);
void gpu_cmd_set_polygon_state(gpu_commands*, VkPrimitiveTopology, VkPolygonMode, f32 line_width);
void gpu_cmd_set_cull_state(   gpu_commands*, VkCullModeFlagBits, VkFrontFace);
void gpu_cmd_set_depth_state(  gpu_commands*, flags<gpu_depth_enable> enabled, VkCompareOp);
void gpu_cmd_set_blend_state(  gpu_commands*, std::span<const gpu_blend_mode>);
void gpu_cmd_bind_shaders(     gpu_commands*, std::span<gpu_shader* const>);

void gpu_cmd_reset_graphics_state(gpu_commands*);

// -----------------------------------------------------------------------------

constexpr static u32 gpu_dma_max_planes = 4;

struct gpu_dma_plane
{
    core_fd fd;
    u32     offset;
    u32     stride;
};

struct gpu_dma_params
{
    core_fixed_array<gpu_dma_plane, gpu_dma_max_planes> planes;
    bool disjoint;

    vec2u32          extent;
    gpu_format       format;
    gpu_drm_modifier modifier;
};

auto gpu_image_import(gpu_context*, const gpu_dma_params&, flags<gpu_image_usage>) -> ref<gpu_image>;
auto gpu_image_export(gpu_image*) -> gpu_dma_params;

// -----------------------------------------------------------------------------

template<typename T>
struct gpu_image_handle
{
    u32 image   : 20 = {};
    u32 sampler : 12 = {};

    gpu_image_handle() = default;

    gpu_image_handle(gpu_image* image, gpu_sampler* sampler)
        : image(std::to_underlying(image->descriptor()))
        , sampler(sampler ? std::to_underlying(sampler->id) : 0)
    {}
};

// -----------------------------------------------------------------------------

template<typename Lessor>
struct gpu_image_lease : gpu_image
{
    ref<gpu_image> image;
    Lessor         lessor;

    gpu_image_lease(ref<gpu_image> image, Lessor&& lessor)
        : image(std::move(image))
        , lessor(std::move(lessor))
    {}

    virtual auto base() -> gpu_image* final override { return image->base(); }

    ~gpu_image_lease()
    {
        lessor(std::move(image));
    }
};

template<typename Lessor>
auto gpu_lease_image(ref<gpu_image> image, Lessor&& lessor) -> ref<gpu_image_lease<Lessor>>
{
    return core_create<gpu_image_lease<Lessor>>(std::move(image), std::move(lessor));
}

// -----------------------------------------------------------------------------

struct gpu_image_pool
{
    virtual ~gpu_image_pool() = default;

    virtual auto acquire(const gpu_image_create_info&) -> ref<gpu_image> = 0;
};

auto gpu_image_pool_create(gpu_context*) -> ref<gpu_image_pool>;
