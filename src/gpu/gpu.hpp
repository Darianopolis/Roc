#pragma once

#include "core/object.hpp"
#include "core/types.hpp"
#include "core/event.hpp"
#include "core/fd.hpp"
#include "core/flags.hpp"

#include "functions.hpp"

// -----------------------------------------------------------------------------

struct gpu_image;
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

    gpu_descriptor_id allocate();
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

std::span<const gpu_format_info> gpu_get_format_infos();

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

gpu_format gpu_format_from_drm(gpu_drm_format);
gpu_format gpu_format_from_vk(VkFormat, flags<gpu_vk_format_flag> = {});

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

    const gpu_format_modifier_props* for_mod(gpu_drm_modifier mod) const
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

    const gpu_format_modifier_set& get(gpu_format format) const noexcept
    {
        auto iter = entries.find(format);
        return iter == entries.end() ? gpu_empty_modifier_set : iter->second;
    }

    usz   size() const { return entries.size(); }
    bool empty() const { return !entries.empty(); }

    auto begin() const { return entries.begin(); }
    auto   end() const { return entries.end(); }
};

gpu_format_modifier_set gpu_intersect_format_modifiers(std::span<const gpu_format_modifier_set* const> sets);
gpu_format_set gpu_intersect_format_sets(std::span<const gpu_format_set* const> sets);

const gpu_format_props* gpu_get_format_props(gpu_context*, gpu_format, flags<gpu_image_usage>);

std::string gpu_drm_modifier_get_name(gpu_drm_modifier);

// -----------------------------------------------------------------------------

enum class gpu_feature : u32
{
    validation = 1 << 0,
};

struct gpu_context : core_object
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

ref<gpu_context> gpu_create(flags<gpu_feature>, core_event_loop*);

// -----------------------------------------------------------------------------

enum class gpu_queue_type : u32
{
    graphics,
    transfer,
};

struct gpu_queue : core_object
{
    gpu_context* ctx;

    gpu_queue_type type;
    u32 family;
    VkQueue queue;

    VkCommandPool cmd_pool;
    VkCommandBuffer cmd;

    ref<gpu_semaphore> queue_sema;

    u64 submitted;

    ~gpu_queue();
};

gpu_queue* gpu_get_queue(gpu_context*, gpu_queue_type);

// -----------------------------------------------------------------------------

using gpu_semaphore_wait_fn = void(u64);

struct gpu_semaphore : core_object
{
    gpu_context* ctx;

    VkSemaphore semaphore;
    u32         syncobj;

    core_fd wait_fd;
    u64 wait_skips = 0;
    struct wait_item : core_intrusive_list_base<wait_item>
    {
        u64 point;
        virtual void handle(u64) = 0;
        virtual ~wait_item() = default;
    };
    core_intrusive_list<wait_item> waits;

    ~gpu_semaphore();
};

struct gpu_syncpoint
{
    gpu_semaphore* semaphore;
    u64 value = 0;
    VkPipelineStageFlags2 stages = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
};

ref<gpu_semaphore> gpu_semaphore_create(gpu_context*);
ref<gpu_semaphore> gpu_semaphore_import_syncobj(gpu_context*, int syncobj_fd);
int gpu_semaphore_export_syncobj(gpu_semaphore*);

void gpu_semaphore_import_syncfile(gpu_semaphore*, int sync_fd, u64 target_point);
int  gpu_semaphore_export_syncfile(gpu_semaphore*, u64 source_point);

u64  gpu_semaphore_get_value(   gpu_semaphore*);
void gpu_semaphore_signal_value(gpu_semaphore*, u64 value);
void gpu_semaphore_wait_value(  gpu_semaphore*, u64 value);

void gpu_semaphore_wait_value_impl(gpu_semaphore*, gpu_semaphore::wait_item*);

template<typename Fn>
void gpu_semaphore_wait_value(gpu_semaphore* semaphore, u64 value, Fn&& fn)
{
    struct wait_item : gpu_semaphore::wait_item
    {
        Fn fn;
        wait_item(Fn&& fn): fn(std::move(fn)) {}
        virtual void handle(u64 value) final override { fn(value); }
    };
    auto wait = new wait_item(std::move(fn));
    wait->point = value;
    gpu_semaphore_wait_value_impl(semaphore, wait);
}

// -----------------------------------------------------------------------------

struct gpu_commands : core_object
{
    gpu_queue* queue;

    VkCommandBuffer buffer;
    std::vector<ref<core_object>> objects;

    u64 submitted_value;

    ~gpu_commands();
};

ref<gpu_commands> gpu_commands_begin(gpu_queue*);

void gpu_commands_protect_object(  gpu_commands*, core_object*);
gpu_syncpoint gpu_commands_submit(gpu_commands*, std::span<const gpu_syncpoint> waits);

// TODO: This is a blocking operation and a temporary solution
//       Replace with an asynchronous callback
void gpu_wait_idle(gpu_context*);
void gpu_wait_idle(gpu_queue*);

// -----------------------------------------------------------------------------

struct gpu_buffer : core_object
{
    gpu_context* ctx;

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

ref<gpu_buffer> gpu_buffer_create(gpu_context*, usz size, flags<gpu_buffer_flag>);

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

    gpu_array_element_proxy<T> operator[](usz index) const
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

VkImageUsageFlags gpu_image_usage_to_vk(flags<gpu_image_usage>);

struct gpu_image : core_object
{
    gpu_context* ctx;

    gpu_format format;

    VkImage image;
    VkImageView view;
    vec2u32 extent;

    gpu_descriptor_id id;

    flags<gpu_image_usage> usage;

    virtual ~gpu_image() = 0;
};

ref<gpu_image> gpu_image_create(gpu_context*, vec2u32 extent, gpu_format, flags<gpu_image_usage>);

void gpu_copy_image_to_buffer(gpu_commands*, gpu_buffer*, gpu_image*);
void gpu_copy_buffer_to_image(gpu_commands*, gpu_image*, gpu_buffer*);

void gpu_image_update(gpu_commands*, gpu_image*, const void* data);
void gpu_image_update_immed(gpu_image*, const void* data);

void gpu_transition(gpu_context* vk, gpu_commands*, gpu_image*,
        VkPipelineStageFlags2 src, VkPipelineStageFlags2 dst,
        VkAccessFlags2 src_access, VkAccessFlags2 dst_access,
        VkImageLayout old_layout, VkImageLayout new_layout);

// -----------------------------------------------------------------------------

struct gpu_sampler : core_object
{
    gpu_context* ctx;

    VkSampler sampler;

    gpu_descriptor_id id;

    ~gpu_sampler();
};

ref<gpu_sampler> gpu_sampler_create(gpu_context*, VkFilter mag, VkFilter min);

// -----------------------------------------------------------------------------

enum class gpu_blend_mode : u32
{
    none,
    premultiplied,
    postmultiplied,
};

struct gpu_pipeline : core_object
{
    gpu_context* ctx;

    VkPipeline pipeline;

    ~gpu_pipeline();
};

ref<gpu_pipeline> gpu_pipeline_create_graphics(gpu_context*,
    gpu_blend_mode,
    gpu_format,
    std::span<const u32> spirv,
    const char* vertex_entry,
    const char* fragment_entry);

ref<gpu_pipeline> gpu_pipeline_create_compute(gpu_context*,
    std::span<const u32> spirv,
    const char* entry);

// -----------------------------------------------------------------------------

constexpr static u32 gpu_dma_max_planes = 4;

struct gpu_dma_plane
{
    core_fd fd;
    u32 offset;
    u32 stride;
};

struct gpu_dma_params
{
    core_fixed_array<gpu_dma_plane, gpu_dma_max_planes> planes;
    bool disjoint;

    vec2u32 extent;
    gpu_format format;
    gpu_drm_modifier modifier;
};

struct gpu_image_dmabuf : gpu_image
{
    core_fixed_array<VkDeviceMemory, gpu_dma_max_planes> memory;
    gpu_drm_modifier modifier;

    struct {
        usz allocation_size;
    } stats;

    ~gpu_image_dmabuf();
};

VkImageAspectFlagBits gpu_plane_to_aspect(u32 i);

ref<gpu_image_dmabuf> gpu_image_create_dmabuf(gpu_context*, vec2u32 extent, gpu_format, flags<gpu_image_usage>, std::span<const gpu_drm_modifier>);
ref<gpu_image_dmabuf> gpu_image_import_dmabuf(gpu_context*, const gpu_dma_params&, flags<gpu_image_usage>);

gpu_dma_params gpu_image_export_dmabuf(gpu_image*);

// -----------------------------------------------------------------------------

template<typename T>
struct gpu_image_handle
{
    u32 image   : 20 = {};
    u32 sampler : 12 = {};

    gpu_image_handle() = default;

    gpu_image_handle(gpu_image* image, gpu_sampler* sampler)
        : image(std::to_underlying(image->id))
        , sampler(sampler ? std::to_underlying(sampler->id) : 0)
    {}
};
