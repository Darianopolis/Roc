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

namespace gpu
{
    struct Image;
    struct Buffer;
    struct Sampler;
    struct Semaphore;
    struct Commands;
    struct Queue;
    struct Context;

    enum class ImageUsage : u32;
}

// -----------------------------------------------------------------------------

namespace gpu
{
    enum class DescriptorId : u32 { invalid = 0 };

    struct DescriptorIdAllocator
    {
        std::vector<gpu::DescriptorId> freelist;
        u32 next_id;
        u32 capacity;

        DescriptorIdAllocator() = default;
        DescriptorIdAllocator(u32 count);

        auto allocate() -> gpu::DescriptorId;
        void free(gpu::DescriptorId);
    };
}

// -----------------------------------------------------------------------------

namespace gpu
{
    using DrmFormat   = u32;
    using DrmModifier = u64;
}

namespace gpu::vk
{
    // Additional core::Flags required to uniquely identify a format when paired with a VkFormat
    enum class FormatFlag : u32
    {
        // DRM FourCC codes have format variants to ignore alpha channels (E.g. XRGB|ARGB).
        // Vulkan handles these in image view channel swizzles, instead of formats.
        ignore_alpha = 1 << 0,
    };
}

namespace gpu
{
    struct FormatInfo
    {
        std::string name;

        bool is_ycbcr;

        gpu::DrmFormat drm;

        VkFormat vk;
        VkFormat vk_srgb;
        core::Flags<gpu::vk::FormatFlag> vk_flags;

        VKU_FORMAT_INFO info;
    };

    auto get_format_infos() -> std::span<const gpu::FormatInfo>;

    struct Format
    {
        // Formats are stored as indices into the static format_infos table.
        u8 index;

        constexpr bool operator==(const gpu::Format&) const noexcept = default;

        constexpr explicit operator bool() const noexcept { return index; }

        constexpr const gpu::FormatInfo* operator->() const noexcept
        {
            return &gpu::get_format_infos()[index];
        }
    };
}

CORE_MAKE_STRUCT_HASHABLE(gpu::Format, v.index);

namespace gpu
{
    inline
    auto get_formats()
    {
        return std::views::iota(0)
            | std::views::take(gpu::get_format_infos().size())
            | std::views::transform([](usz i) { return gpu::Format(i); });
    }
}

namespace gpu::format
{
    auto from_drm(gpu::DrmFormat) -> gpu::Format;
    auto from_vk(VkFormat, core::Flags<gpu::vk::FormatFlag> = {}) -> gpu::Format;
}

namespace gpu
{
    struct FormatModifierProps
    {
        gpu::DrmModifier modifier;
        VkFormatFeatureFlags2 features;
        u32 plane_count;

        VkExternalMemoryProperties ext_mem_props;

        vec2u32 max_extent;
        bool has_mutable_srgb;
    };

    using FormatModifierSet = core::FlatSet<gpu::DrmModifier>;
    inline const gpu::FormatModifierSet empty_modifier_set;

    struct FormatProps
    {
        std::unique_ptr<gpu::FormatModifierProps> opt_props;
        std::vector<gpu::FormatModifierProps> mod_props;
        gpu::FormatModifierSet mods;

        auto for_mod(gpu::DrmModifier mod) const -> const gpu::FormatModifierProps*
        {
            for (auto& p : mod_props) {
                if (p.modifier == mod) return &p;
            }
            return nullptr;
        }
    };

    struct FormatPropsKeys
    {
        VkFormat format;
        VkImageUsageFlags usage;

        constexpr bool operator==(const gpu::FormatPropsKeys&) const noexcept = default;
    };
}

CORE_MAKE_STRUCT_HASHABLE(gpu::FormatPropsKeys, v.format, v.usage);

namespace gpu
{
    struct FormatSet
    {
        core::Map<gpu::Format, gpu::FormatModifierSet> entries;

        void add(gpu::Format format, gpu::DrmModifier modifier)
        {
            entries[format].insert(modifier);
        }

        void clear() { entries.clear(); }

        auto get(gpu::Format format) const noexcept -> const gpu::FormatModifierSet&
        {
            auto iter = entries.find(format);
            return iter == entries.end() ? gpu::empty_modifier_set : iter->second;
        }

        usz   size() const { return entries.size(); }
        bool empty() const { return !entries.empty(); }

        auto begin() const { return entries.begin(); }
        auto   end() const { return entries.end(); }
    };

    auto intersect_format_modifiers(std::span<const gpu::FormatModifierSet* const> sets) -> gpu::FormatModifierSet;
    auto intersect_format_sets(std::span<const gpu::FormatSet* const> sets) -> gpu::FormatSet;

    auto get_format_props(gpu::Context*, gpu::Format, core::Flags<gpu::ImageUsage>) -> const gpu::FormatProps*;

    auto drm_modifier_get_name(gpu::DrmModifier) -> std::string;
}

// -----------------------------------------------------------------------------

namespace gpu
{
    enum class Feature : u32
    {
        validation = 1 << 0,
    };

    struct Context
    {
        core::Flags<gpu::Feature> features;

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

        core::Ref<gpu::Queue> graphics_queue;
        core::Ref<gpu::Queue> transfer_queue;
        core::Ref<core::EventLoop> event_loop;

        std::vector<VkSemaphore> free_binary_semaphores;

        VkDescriptorSetLayout set_layout;
        VkPipelineLayout pipeline_layout;
        VkDescriptorPool pool;
        VkDescriptorSet set;

        gpu::DescriptorIdAllocator image_descriptor_allocator;
        gpu::DescriptorIdAllocator sampler_descriptor_allocator;

        core::SegmentedMap<gpu::FormatPropsKeys, gpu::FormatProps> format_props;

        ~Context();
    };

    auto create(core::Flags<gpu::Feature>, core::EventLoop*) -> core::Ref<gpu::Context>;
}

// -----------------------------------------------------------------------------

namespace gpu
{
    enum class QueueType : u32
    {
        graphics,
        transfer,
    };

    struct Queue
    {
        gpu::Context* gpu;

        gpu::QueueType type;
        u32 family;
        VkQueue queue;

        VkCommandPool cmd_pool;
        VkCommandBuffer cmd;

        core::Ref<gpu::Semaphore> queue_sema;

        u64 submitted;

        ~Queue();
    };
}

namespace gpu::queue
{
    auto get(gpu::Context*, gpu::QueueType) -> gpu::Queue*;
}

// -----------------------------------------------------------------------------

namespace gpu
{
    struct WaitFn : core::IntrusiveListBase<gpu::WaitFn>
    {
        u64 point;

        virtual void handle(u64 point) = 0;
        virtual ~WaitFn() = default;
    };

    struct Semaphore
    {
        gpu::Context* gpu;

        VkSemaphore semaphore;
        u32         syncobj;

        struct {
            core::Fd fd;
            u64 skips = 0;
            core::IntrusiveList<gpu::WaitFn> list;
        } wait;

        ~Semaphore();
    };

    struct Syncpoint
    {
        gpu::Semaphore* semaphore;
        u64 value = 0;
        VkPipelineStageFlags2 stages = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    };
}

namespace gpu::semaphore
{
    auto create(gpu::Context*) -> core::Ref<gpu::Semaphore>;
    auto import_syncobj(gpu::Context*, int syncobj_fd) -> core::Ref<gpu::Semaphore>;
    int  export_syncobj(gpu::Semaphore*);

    void import_syncfile(gpu::Semaphore*, int sync_fd, u64 target_point);
    int  export_syncfile(gpu::Semaphore*, u64 source_point);

    u64  get_value(   gpu::Semaphore*);
    void signal_value(gpu::Semaphore*, u64 value);

    void wait(gpu::Semaphore*, gpu::WaitFn*);
}

namespace gpu
{
    // WARNING: Blocking
    void wait(gpu::Syncpoint);

    template<typename Fn>
    void wait(gpu::Syncpoint sync, Fn&& fn)
    {
        struct wait_item : gpu::WaitFn
        {
            Fn fn;
            wait_item(Fn&& fn): fn(std::move(fn)) {}
            virtual void handle(u64 value) final override { fn(value); }
        };
        auto wait = new wait_item(std::move(fn));
        wait->point = sync.value;
        gpu::semaphore::wait(sync.semaphore, wait);
    }
}

// -----------------------------------------------------------------------------

namespace gpu
{
    struct Commands
    {
        gpu::Queue* queue;

        VkCommandBuffer buffer;
        std::vector<core::Ref<void>> objects;

        u64 submitted_value;

        ~Commands();
    };
}

namespace gpu::commands
{
    auto begin(gpu::Queue*) -> core::Ref<gpu::Commands>;

    void protect(gpu::Commands*, core::Ref<void>);
}

namespace gpu
{
    auto submit(gpu::Commands*, std::span<const gpu::Syncpoint> waits) -> gpu::Syncpoint;

    // WARNING: Blocking
    void wait_idle(gpu::Context*);

    // WARNING: Blocking
    void wait_idle(gpu::Queue*);
}

// -----------------------------------------------------------------------------

namespace gpu
{
    struct Buffer
    {
        gpu::Context* gpu;

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
            return core::byte_offset_pointer<T>(host_address, byte_offset);
        }

        ~Buffer();
    };

    enum class BufferFlag : u32
    {
        host = 1 << 0,
    };
}

namespace gpu::buffer
{
    auto create(gpu::Context*, usz size, core::Flags<gpu::BufferFlag>) -> core::Ref<gpu::Buffer>;
}

// -----------------------------------------------------------------------------

namespace gpu
{
    template<typename T>
    struct ArrayElementProxy
    {
        T* host_value;

        void operator=(const T& value)
        {
            std::memcpy(host_value, &value, sizeof(value));
        }
    };

    template<typename T>
    struct Array
    {
        core::Ref<gpu::Buffer> buffer;
        usz count = 0;
        usz byte_offset = 0;

        Array() = default;

        Array(const auto& buffer, usz count, usz byte_offset = {})
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

        auto operator[](usz index) const -> gpu::ArrayElementProxy<T>
        {
            return {buffer->host<T>(byte_offset) + index};
        }
    };
}

// -----------------------------------------------------------------------------

namespace gpu
{
    enum class ImageUsage : u32
    {
        transfer_src = 1 << 0,
        transfer_dst = 1 << 1,
        transfer     = transfer_dst | transfer_src,
        texture      = 1 << 2,
        render       = 1 << 3,
        storage      = 1 << 4,
    };

    struct Image
    {
        virtual ~Image() = default;

        virtual auto base() -> gpu::Image* = 0;

        auto context()    -> gpu::Context*;
        auto extent()     -> vec2u32;
        auto format()     -> gpu::Format;
        auto modifier()   -> gpu::DrmModifier;
        auto view()       -> VkImageView;
        auto handle()     -> VkImage;
        auto usage()      -> core::Flags<gpu::ImageUsage>;
        auto descriptor() -> gpu::DescriptorId;
    };

    struct ImageCreateInfo
    {
        vec2u32                        extent;
        gpu::Format                     format;
        core::Flags<gpu::ImageUsage>         usage;
        const gpu::FormatModifierSet* modifiers;
    };
}

namespace gpu::image
{
    auto create(gpu::Context*, const gpu::ImageCreateInfo&) -> core::Ref<gpu::Image>;
}

namespace gpu
{
    struct BufferImageCopy
    {
        vec2u32 image_extent;
        vec2i32 image_offset;
        u32 buffer_offset;
        u32 buffer_row_length;
    };
}

namespace gpu::commands
{
    void copy_image_to_buffer(gpu::Commands*, gpu::Buffer*, gpu::Image*);

    void copy_buffer_to_image(gpu::Commands*, gpu::Image*, gpu::Buffer*, std::span<const gpu::BufferImageCopy> regions);
    void copy_memory_to_image(gpu::Commands*, gpu::Image*, core::ByteView data, std::span<const gpu::BufferImageCopy> regions);
}

namespace gpu
{
    // WARNING: Blocking
    void copy_memory_to_image(gpu::Image*, core::ByteView data, std::span<const gpu::BufferImageCopy> regions);
}

namespace gpu::image
{
    auto compute_linear_offset(gpu::Format, vec2u32 position, u32 stride) -> u32;
}

// -----------------------------------------------------------------------------

namespace gpu
{
    struct Sampler
    {
        gpu::Context* gpu;

        VkSampler sampler;

        gpu::DescriptorId id;

        ~Sampler();
    };

    struct SamplerCreateInfo
    {
        VkFilter mag;
        VkFilter min;
    };
}

namespace gpu::sampler
{
    auto create(gpu::Context*, const gpu::SamplerCreateInfo&) -> core::Ref<gpu::Sampler>;
}

// -----------------------------------------------------------------------------

namespace gpu
{
    enum class BlendMode : u32
    {
        none,
        premultiplied,
        postmultiplied,
    };

    struct Shader;

    struct ShaderCreateInfo
    {
        VkShaderStageFlagBits stage;
        std::span<const u32>  code;
        const char*           entry;
    };
}

namespace gpu::shader
{
    auto create(gpu::Context*, const gpu::ShaderCreateInfo&) -> core::Ref<gpu::Shader>;
}

namespace gpu
{
    enum class DepthEnable
    {
        test  = 1 << 0,
        write = 1 << 1,
    };
}

namespace gpu::commands
{
    void push_constants(   gpu::Commands*, u32 offset, core::ByteView data);
    void set_scissors(     gpu::Commands*, std::span<const rect2i32> scissors);
    void set_viewports(    gpu::Commands*, std::span<const rect2f32> viewports);
    void set_polygon_state(gpu::Commands*, VkPrimitiveTopology, VkPolygonMode, f32 line_width);
    void set_cull_state(   gpu::Commands*, VkCullModeFlagBits, VkFrontFace);
    void set_depth_state(  gpu::Commands*, core::Flags<gpu::DepthEnable> enabled, VkCompareOp);
    void set_blend_state(  gpu::Commands*, std::span<const gpu::BlendMode>);
    void bind_shaders(     gpu::Commands*, std::span<gpu::Shader* const>);

    void reset_graphics_state(gpu::Commands*);
}

// -----------------------------------------------------------------------------

namespace gpu
{
    constexpr static u32 max_dma_planes = 4;

    struct DmaPlane
    {
        core::Fd fd;
        u32     offset;
        u32     stride;
    };

    struct DmaParams
    {
        core::FixedArray<gpu::DmaPlane, gpu::max_dma_planes> planes;
        bool disjoint;

        vec2u32          extent;
        gpu::Format       format;
        gpu::DrmModifier modifier;
    };
}

namespace gpu::image
{
    auto import_dmabuf(gpu::Context*, const gpu::DmaParams&, core::Flags<gpu::ImageUsage>) -> core::Ref<gpu::Image>;
    auto export_dmabuf(gpu::Image*) -> gpu::DmaParams;
}

// -----------------------------------------------------------------------------

namespace gpu
{
    template<typename T>
    struct ImageHandle
    {
        u32 image   : 20 = {};
        u32 sampler : 12 = {};

        ImageHandle() = default;

        ImageHandle(gpu::Image* image, gpu::Sampler* sampler)
            : image(std::to_underlying(image->descriptor()))
            , sampler(sampler ? std::to_underlying(sampler->id) : 0)
        {}
    };
}

// -----------------------------------------------------------------------------

namespace gpu
{
    template<typename Lessor>
    struct ImageLease : gpu::Image
    {
        core::Ref<gpu::Image> image;
        Lessor         lessor;

        ImageLease(core::Ref<gpu::Image> image, Lessor&& lessor)
            : image(std::move(image))
            , lessor(std::move(lessor))
        {}

        virtual auto base() -> gpu::Image* final override { return image->base(); }

        ~ImageLease()
        {
            lessor(std::move(image));
        }
    };
}


namespace gpu::image
{
    template<typename Lessor>
    auto lease(core::Ref<gpu::Image> image, Lessor&& lessor) -> core::Ref<gpu::ImageLease<Lessor>>
    {
        return core::create<gpu::ImageLease<Lessor>>(std::move(image), std::move(lessor));
    }
}

// -----------------------------------------------------------------------------

namespace gpu
{
    struct ImagePool
    {
        virtual ~ImagePool() = default;

        virtual auto acquire(const gpu::ImageCreateInfo&) -> core::Ref<gpu::Image> = 0;
    };
}

namespace gpu::image_pool
{
    auto create(gpu::Context*) -> core::Ref<gpu::ImagePool>;
}
