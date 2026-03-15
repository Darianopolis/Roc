#pragma once

#include "util.hpp"

#include "core/pch.hpp"
#include "core/types.hpp"
#include "core/util.hpp"
#include "core/log.hpp"
#include "core/region.hpp"
#include "core/event.hpp"

#include "gpu/gpu.hpp"

struct wroc_server;
struct wroc_renderer;
struct wroc_output;
struct wroc_surface;
struct wroc_toplevel;
struct wroc_buffer;
struct wroc_buffer_lock;
struct wroc_cursor_surface;
struct wroc_seat_keyboard;
struct wroc_seat_pointer;
struct wroc_pointer_constraint;
struct wroc_data_offer;
struct wroc_data_device;
struct wroc_imgui;

// -----------------------------------------------------------------------------

void wroc_run(int argc, char* argv[]);
void wroc_terminate();

// -----------------------------------------------------------------------------

enum struct wroc_backend_type
{
    layered,
    direct,
};

struct wroc_backend : wroc_object
{
    wroc_backend_type type;

    virtual void init() = 0;
    virtual void start() = 0;

    virtual const gpu_format_set& get_output_format_set() = 0;

    virtual void create_output() = 0;
    virtual void destroy_output(wroc_output*) = 0;
};

core::Ref<wroc_backend> wroc_backend_create(wroc_backend_type);

// -----------------------------------------------------------------------------

struct wroc_coord_space
{
    vec2f64 origin;
    vec2f64 scale;

    constexpr vec2f64 from_global(vec2f64 global_pos) const
    {
        return (global_pos - origin) / scale;
    }

    constexpr vec2f64 to_global(vec2f64 local_pos) const
    {
        return (local_pos * scale) + origin;
    }

    constexpr rect2f64 from_global(rect2f64 global_rect) const
    {
        return { from_global(global_rect.origin), global_rect.extent / scale, core::xywh };
    }

    constexpr rect2f64 to_global(rect2f64 global_rect) const
    {
        return { to_global(global_rect.origin), global_rect.extent * scale, core::xywh };
    }
};

// -----------------------------------------------------------------------------

struct wroc_output_mode
{
    vec2i32 size;
    f64 refresh;
};

struct wroc_output_desc
{
    std::string make;
    std::string model;
    std::string name;
    std::string description;
    vec2f64 physical_size_mm;
    wl_output_subpixel subpixel = WL_OUTPUT_SUBPIXEL_UNKNOWN;
    wl_output_transform transform = WL_OUTPUT_TRANSFORM_NORMAL;
    f64 scale;

    std::vector<wroc_output_mode> modes;
    u32 current_mode;
    u32 preferred_mode;
};

/*
 * Represents a wl_output protocol object
 * This may or may not correspond to any actual output made available by the backend.
 */
struct wroc_wl_output : wroc_object
{
    wl_global* global;
    wroc_resource_list resources;

    wroc_output_desc desc;
    vec2i32 position;
};

void wroc_output_desc_update(wroc_wl_output*);
void wroc_output_enter_surface(wroc_wl_output*, wroc_surface*);

enum class wroc_output_commit_flag : u32
{
    vsync = 1 << 0,
};

using wroc_output_commit_id = u64;

/*
 * A backend output that can be displayed to
 */
struct wroc_output : wroc_object
{
    vec2u32 size;
    rect2f64 layout_rect;

    wroc_output_desc desc;

    bool frame_available = false;

    u32 frames_in_flight = 0;

    struct release_slot
    {
        core::Ref<gpu_semaphore> semaphore;
        core::Ref<gpu_image> image;
        u64 release_point;
    };
    struct {
        std::vector<core::Ref<gpu_image>> free_images;
        std::vector<release_slot> release_slots;
        u32 images_in_flight;
    } swapchain;

    wroc_output_commit_id last_commit_id = 0;

    std::chrono::steady_clock::time_point last_frame_time = {};

    std::vector<std::chrono::steady_clock::time_point> try_dispatch_queue;

    bool frame_requested = true;

    virtual wroc_output_commit_id commit(gpu_image* image, gpu_syncpoint acquire, gpu_syncpoint release, core::Flags<wroc_output_commit_flag>) = 0;
};

wroc_coord_space wroc_output_get_coord_space(wroc_output*);

bool wroc_output_try_prepare_acquire(     wroc_output*);
bool wroc_output_try_dispatch_frame(      wroc_output*);
void wroc_output_try_dispatch_frame_later(wroc_output*);

void wroc_output_request_frame(wroc_output*);

struct wroc_output_layout : wroc_object
{
    // TODO: Support multi output description for clients
    core::Ref<wroc_wl_output> primary;
    std::vector<core::Weak<wroc_output>> outputs;
};

void wroc_output_layout_init();
vec2f64 wroc_output_layout_clamp_position(wroc_output_layout*, vec2f64 global_pos, wroc_output** output = nullptr);
wroc_output* wroc_output_layout_output_for_surface(wroc_output_layout*, wroc_surface*);

// -----------------------------------------------------------------------------

struct wroc_region : wroc_object
{
    wroc_resource resource;

    region2i32 region;
};

// -----------------------------------------------------------------------------

using wroc_commit_id = u32;

// TODO: Reuse memory for packets
template<typename T>
struct wroc_surface_state_packet
{
    T state;
    wroc_commit_id id;
};

/**
 * Helper base for implementing surface components that must maintain buffered state
 *
 * This is not mandatory, as surface addons simply provide virtual commit/apply implementations.
 * However it is strongly recommended unless an addon has no state or requires special handling.
 */
template<typename T>
struct wroc_surface_state_queue_base
{
    // To facilitate efficient commits, pending state is prepared within the queue itself
    T* pending;
    std::deque<wroc_surface_state_packet<T>> cached;
    T current;

    wroc_surface_state_queue_base()
    {
        pending = &cached.emplace_back().state;
    }
};

template<typename T>
void wroc_surface_state_queue_commit(T* base, wroc_commit_id id)
{
    if (base->pending->committed.empty()) {
        return;
    }
    base->cached.back().id = id;
    base->pending = &base->cached.emplace_back().state;
}

template<typename T, typename ApplyFn>
void wroc_surface_state_queue_apply(T* base, wroc_commit_id id, ApplyFn&& apply_fn)
{
    while (base->cached.size() > 1) {
        auto& packet = base->cached.front();
        if (packet.id > id) break;
        apply_fn(base, packet.state, packet.id);
        base->cached.pop_front();
    }
}

enum class wroc_surface_role : u32
{
    none,
    cursor,
    drag_icon,
    subsurface,
    xdg_toplevel,
    xdg_popup,
};

struct wroc_surface_addon : wroc_object
{
    core::Weak<wroc_surface> surface;

    // Enqueue new state packet with the given commit id
    virtual void commit(wroc_commit_id) = 0;

    // Apply all queued commits up to and including the specified commit id
    virtual void apply(wroc_commit_id) = 0;

    virtual void on_mapped_change() {}
};

void wroc_surface_addon_detach(wroc_surface_addon* addon);

enum class wroc_surface_committed_state : u32
{
    buffer        = 1 << 0,
    offset        = 1 << 1,
    opaque_region = 1 << 2,
    input_region  = 1 << 3,
    buffer_scale  = 1 << 4,

    // Subsurface related state
    surface_stack = 1 << 5,
    parent_commit = 1 << 6,
};

struct wroc_surface_stack_element
{
    core::Weak<wroc_surface> surface;
    vec2i32 position;
};

struct wroc_surface_state
{
    core::Flags<wroc_surface_committed_state> committed;

    core::Ref<wroc_buffer> buffer;
    core::Ref<wroc_buffer_lock> buffer_lock;
    wroc_resource_list frame_callbacks;
    vec2i32 delta;
    region2i32 opaque_region;
    region2i32 input_region;
    f64 buffer_scale;
    wroc_commit_id parent_commit;

    // This tracks layer and position for all subsurfaces in the stack
    // This state is set from the subsurface, but owned and updated on the parent surface
    std::vector<wroc_surface_stack_element> surface_stack;

    ~wroc_surface_state();
};

struct wroc_surface : wroc_object, wroc_surface_state_queue_base<wroc_surface_state>
{
    wroc_resource resource;

    wroc_commit_id committed = 0;
    wroc_commit_id applied = 0;

    wroc_surface_role role = wroc_surface_role::none;
    core::Weak<wroc_surface_addon> role_addon;
    std::vector<core::Ref<wroc_surface_addon>> addons;

    core::Weak<wroc_surface> cursor;

    // In surface coordinates, origin represents "offset" surface property
    rect2i32 buffer_dst;

    // In buffer coordinates
    rect2f64 buffer_src;

    bool mapped;

    bool apply_queued = false;

    ~wroc_surface();
};

// Attempts to dequeue state packets
void wroc_surface_flush_apply(wroc_surface*);

void wroc_surface_update_map_state(wroc_surface*);

rect2f64 wroc_surface_get_frame(wroc_surface*);
wroc_coord_space wroc_surface_get_coord_space(wroc_surface*);
vec2f64 wroc_surface_pos_from_global(wroc_surface*, vec2f64 global_pos);
vec2f64 wroc_surface_pos_to_global(wroc_surface*, vec2f64 surface_pos);

bool wroc_surface_point_accepts_input(wroc_surface*, vec2f64 surface_pos);

void wroc_surface_raise(wroc_surface*);
bool wroc_surface_is_focusable(wroc_surface*);

bool wroc_surface_put_addon_impl(wroc_surface*, wroc_surface_addon*, wroc_surface_role);

template<typename T>
bool wroc_surface_put_addon(wroc_surface* surface, T* addon)
{
    return wroc_surface_put_addon_impl(surface, addon, T::role);
}

wroc_surface_addon* wroc_surface_get_role_addon(wroc_surface*, wroc_surface_role);
wroc_surface_addon* wroc_surface_get_addon(wroc_surface*, const std::type_info&);
template<typename T>
T* wroc_surface_get_addon(wroc_surface* surface)
{
    if constexpr (T::role != wroc_surface_role::none) {
        return static_cast<T*>(wroc_surface_get_role_addon(surface, T::role));
    } else {
        return static_cast<T*>(wroc_surface_get_addon(surface, typeid(T)));
    }
}

template<typename T>
T* wroc_surface_get_or_create_addon(wroc_surface* surface, bool* created = nullptr)
{
    if (created) *created = false;
    auto* existing = wroc_surface_get_addon<T>(surface);
    if (existing) return existing;
    auto addon = core::create<T>();
    if (!wroc_surface_put_addon(surface, addon.get())) return nullptr;
    if (created) *created = true;
    return addon.get();
}

// -----------------------------------------------------------------------------

enum class wroc_viewport_committed_state : u32
{
    source      = 1 << 0,
    destination = 1 << 1,
};

struct wroc_viewport_state
{
    core::Flags<wroc_viewport_committed_state> committed;
    rect2f64 source;
    vec2i32 destination;
};

struct wroc_viewport : wroc_surface_addon, wroc_surface_state_queue_base<wroc_viewport_state>
{
    static constexpr wroc_surface_role role = wroc_surface_role::none;

    core::Weak<wroc_surface> parent;

    wroc_resource resource;

    virtual void commit(wroc_commit_id) final override;
    virtual void apply(wroc_commit_id) final override;
};

// -----------------------------------------------------------------------------

#define WROC_NOISY_SUBSURFACES 0

struct wroc_subsurface : wroc_surface_addon
{
    static constexpr wroc_surface_role role = wroc_surface_role::subsurface;

    core::Weak<wroc_surface> parent;

    wroc_resource resource;

    // TODO: This is a temporary transitionary helper until we have a proper scene graph
    vec2i32 position() const;

    bool synchronized = true;
    bool last_synchronized = true;

    virtual void commit(wroc_commit_id) final override;
    virtual void apply(wroc_commit_id) final override {};
};

wroc_surface* wroc_subsurface_get_root_surface(wroc_subsurface*);

// -----------------------------------------------------------------------------

enum class wroc_xdg_surface_committed_state : u32
{
    geometry = 1 << 0,
    ack      = 1 << 1,
};

struct wrox_xdg_surface_state
{
    core::Flags<wroc_xdg_surface_committed_state> committed = {};

    rect2i32 geometry;
    u32 acked_serial;
};

struct wroc_xdg_surface : wroc_surface_addon, wroc_surface_state_queue_base<wrox_xdg_surface_state>
{
    static constexpr wroc_surface_role role = wroc_surface_role::none;

    wroc_resource resource;

    u32 sent_serial = {};
    u32 acked_serial = {};

    virtual void commit(wroc_commit_id) final override;
    virtual void apply(wroc_commit_id) final override;
};

rect2i32 wroc_xdg_surface_get_geometry(wroc_xdg_surface*);
void wroc_xdg_surface_flush_configure(wroc_xdg_surface*);

// -----------------------------------------------------------------------------

struct wroc_xdg_shell_role_addon : wroc_surface_addon
{
    wroc_xdg_surface* base() const { return wroc_surface_get_addon<wroc_xdg_surface>(surface.get()); }
};

// -----------------------------------------------------------------------------

enum class wroc_xdg_toplevel_configure_state : u32
{
    bounds = 1 << 0,
    size   = 1 << 1,
    states = 1 << 2,
};

enum class wroc_xdg_toplevel_committed_state : u32
{
    parent = 1 << 0,
    title  = 1 << 1,
    app_id = 1 << 2,
};

struct wroc_xdg_toplevel_state
{
    core::Flags<wroc_xdg_toplevel_committed_state> committed = {};

    core::Weak<wroc_toplevel> parent;
    std::string title;
    std::string app_id;
};

struct wroc_toplevel : wroc_xdg_shell_role_addon, wroc_surface_state_queue_base<wroc_xdg_toplevel_state>
{
    static constexpr wroc_surface_role role = wroc_surface_role::xdg_toplevel;

    wroc_resource resource;

    bool initial_configure_complete;
    bool initial_size_received;

    struct configure {
        vec2i32 bounds;
        vec2i32 size;
        std::vector<xdg_toplevel_state> states;
        core::Flags<wroc_xdg_toplevel_configure_state> pending = {};
    } configure;

    struct {
        vec2f64 position;
        vec2f64 relative;
    } anchor;

    std::optional<vec2f64> layout_size;

    struct {
        vec2i32 prev_size;
        core::Weak<wroc_output> output;
    } fullscreen;

    struct {
        bool force_accel = false;
    } tweaks;

    virtual void commit(wroc_commit_id) final override;
    virtual void apply(wroc_commit_id) final override;

    virtual void on_mapped_change() final override;
};

void wroc_toplevel_set_bounds(wroc_toplevel*, vec2i32 bounds);
void wroc_toplevel_set_size(wroc_toplevel*, vec2i32 size);
void wroc_toplevel_set_state(wroc_toplevel*, xdg_toplevel_state, bool enabled);
void wroc_toplevel_flush_configure(wroc_toplevel*);
void wroc_toplevel_close(wroc_toplevel*);

void wroc_toplevel_set_anchor_relative(wroc_toplevel*, vec2f64 anchor_rel);

/**
 * Update the size of the toplevel in the layout space.
 * This may or may not update the underlying client toplevel size.
 */
void wroc_toplevel_set_layout_size(wroc_toplevel*, vec2i32 size);
void wroc_toplevel_force_rescale(wroc_toplevel*, bool force_rescale);
void wroc_toplevel_set_fullscreen(wroc_toplevel*, wroc_output*);
void wroc_toplevel_update_fullscreen_size(wroc_toplevel*);

rect2f64 wroc_toplevel_get_layout_rect(wroc_toplevel*, rect2i32* geometry = nullptr);

// -----------------------------------------------------------------------------

struct wroc_axis_region
{
    i32 pos;
    i32 size;
};

struct wroc_axis_overlaps
{
    i32 start;
    i32 end;
};

struct wroc_positioner_rules
{
    vec2i32 size;
    rect2i32 anchor_rect;
    xdg_positioner_anchor anchor;
    xdg_positioner_gravity gravity;
    core::Flags<xdg_positioner_constraint_adjustment> constraint_adjustment;
    vec2i32 offset;
    bool reactive = false;
    vec2i32 parent_size;
    u32 parent_configure;
};

struct wroc_positioner : wroc_object
{
    wroc_resource resource;

    wroc_positioner_rules rules;
};

// -----------------------------------------------------------------------------

struct wroc_popup : wroc_xdg_shell_role_addon
{
    static constexpr wroc_surface_role role = wroc_surface_role::xdg_popup;

    wroc_resource resource;

    core::Ref<wroc_positioner> positioner;
    std::optional<u32> reposition_token;

    core::Weak<wroc_xdg_surface> parent;
    core::Weak<wroc_toplevel> root_toplevel;
    bool initial_configure_complete;

    // Position of popup, in surface coordinates, relative to parent surface origin
    vec2f64 position;

    virtual void commit(wroc_commit_id) final override;
    virtual void apply(wroc_commit_id) final override;
};

// -----------------------------------------------------------------------------

enum class wroc_buffer_type : u32
{
    shm,
    dma,
};

struct wroc_buffer : wroc_object
{
    friend wroc_buffer_lock;

    wroc_buffer_type type;

    wroc_resource resource;

    vec2u32 extent;

    core::Ref<gpu_image> image;

    core::Weak<wroc_buffer_lock> lock_guard;
    [[nodiscard]] core::Ref<wroc_buffer_lock> lock();

    [[nodiscard]] core::Ref<wroc_buffer_lock> commit(wroc_surface*);

    bool released = true;
    void release();

    virtual bool is_ready(wroc_surface*) = 0;

protected:
    virtual void on_commit(wroc_surface*) = 0;
    virtual void on_unlock() = 0;
};

struct wroc_buffer_lock : wroc_object
{
    core::Ref<wroc_buffer> buffer;

    ~wroc_buffer_lock();
};

// -----------------------------------------------------------------------------

struct wroc_shm_pool;

struct wroc_shm_mapping
{
    void* data;
    i32 size;

    ~wroc_shm_mapping();
};

struct wroc_shm_pool : wroc_object
{
    wroc_resource resource;

    int fd;
    core::Ref<wroc_shm_mapping> mapping;

    ~wroc_shm_pool();
};

struct wroc_shm_buffer : wroc_buffer
{
    core::Ref<wroc_shm_pool> pool;

    i32 offset;
    i32 stride;
    gpu_format format;

    bool pending_transfer;

    virtual bool is_ready(wroc_surface*) final override;

    virtual void on_commit(wroc_surface*) final override;
    virtual void on_unlock() final override;
};

// -----------------------------------------------------------------------------

struct wroc_dma_buffer_params : wroc_object
{
    wroc_resource resource;

    gpu_dma_params params;
    u32 planes_set;
};

struct wroc_dma_buffer : wroc_buffer
{
    std::optional<gpu_dma_params> params;

    bool needs_wait = false;

    core::Ref<gpu_semaphore> acquire_timeline;
    u64 acquire_point;

    core::Ref<gpu_semaphore> release_timeline;
    u64 release_point;

    virtual bool is_ready(wroc_surface*) final override;

    virtual void on_commit(wroc_surface*) final override;
    virtual void on_unlock() final override;
};

// -----------------------------------------------------------------------------

struct wroc_syncobj_timeline : wroc_object
{
    core::Ref<gpu_semaphore> syncobj;

    wroc_resource resource;
};

struct wroc_syncobj_surface : wroc_surface_addon
{
    static constexpr wroc_surface_role role = wroc_surface_role::none;

    wroc_resource resource;

    core::Ref<gpu_semaphore> release_timeline;
    u64 release_point;

    core::Ref<gpu_semaphore> acquire_timeline;
    u64 acquire_point;

    virtual void commit(wroc_commit_id) final override {};
    virtual void apply(wroc_commit_id) final override {}
};

// -----------------------------------------------------------------------------

struct wroc_seat : wroc_object
{
    core::Ref<wroc_seat_keyboard> keyboard;
    core::Ref<wroc_seat_pointer>  pointer;

    std::string name;

    wroc_resource_list resources;
};

void wroc_seat_init();
void wroc_seat_init_keyboard(wroc_seat*);
void wroc_seat_init_pointer(wroc_seat*);

// -----------------------------------------------------------------------------

enum class wroc_modifier : u32
{
    mod    = 1 << 0,
    super  = 1 << 1,
    shift  = 1 << 2,
    ctrl   = 1 << 3,
    alt    = 1 << 4,
    num    = 1 << 5,
    caps   = 1 << 6,
};

enum class wroc_key_action : u32
{
    press,      // Key was pressed by source
    release,    // Key was released by source
    enter,      // Key was "discovered" in the pressed state (does not trigger "on press" actions)
                // E.g. keyboard enter events in the Wayland client
};

/**
 * Represents a source (evdev) keyboard
 *
 * Source keyboards are "dumb" evdev (KEY_*) key code buckets.
 * We ignore pre-existing state (e.g. modifiers/keymaps) from sources (E.g. nested wayland environments)
 */
struct wroc_keyboard : wroc_object
{
    core::FlatSet<u32> pressed = {};

    core::Weak<wroc_seat_keyboard> target;

    void enter(std::span<const u32>);
    void leave();
    void press(u32);
    void release(u32);

    virtual void update_leds(libinput_led) {};

    ~wroc_keyboard();
};

/*
 * Represents a virtual (xkb) keyboard that is exposed to clients
 *
 * This will typically be an aggregate of several source keyboards
 * XKB keymaps are selected here, modifier states are derived based on source states/events
 * LED event states are sent back to source keyboards.
 */
struct wroc_seat_keyboard : wroc_object
{
    wroc_seat* seat;

    std::vector<wroc_keyboard*> sources;

    // Aggregate of sources[...]->pressed
    core::CountingSet<u32> pressed;

    wroc_resource_list resources;
    core::Weak<wroc_surface> focused_surface;

    struct xkb_context* context;
    struct xkb_state*   state;
    struct xkb_keymap*  keymap;

    core::EnumMap<wroc_modifier, xkb_mod_mask_t> mod_masks;

    int keymap_fd = -1;
    i32 keymap_size;

    i32 rate = 25;
    i32 delay = 600;

    void attach(wroc_keyboard*);

    bool is_locked(wroc_modifier) const;
    void set_locked(wroc_modifier, bool locked);

    ~wroc_seat_keyboard();
};

void wroc_seat_keyboard_send_configuration(wroc_seat_keyboard*, wl_client*, wl_resource*);

core::Flags<wroc_modifier> wroc_keyboard_get_active_modifiers(wroc_seat_keyboard*);

void wroc_keyboard_clear_focus(wroc_seat_keyboard*);
void wroc_keyboard_enter(wroc_seat_keyboard*, wroc_surface*);

// -----------------------------------------------------------------------------

struct wroc_pointer : wroc_object
{
    core::FlatSet<u32> pressed = {};

    core::Weak<wroc_seat_pointer> target;

    vec2f64 rel_remainder;

    void enter(std::span<const u32>);
    void leave();
    void press(u32);
    void release(u32);

    void absolute(wroc_output*, vec2f64 offset);
    void relative(vec2f64 delta);

    void scroll(vec2f64 delta);

    ~wroc_pointer();
};

struct wroc_seat_pointer : wroc_object
{
    wroc_seat* seat;

    // For translating mouse buttons to key presses
    core::Ref<wroc_keyboard> keyboard;

    std::vector<wroc_pointer*> sources;

    // Aggregate of sources[...]->pressed
    core::CountingSet<u32> pressed;

    wroc_resource_list resources;
    wroc_resource_list relative_pointers;
    core::Weak<wroc_surface> focused_surface;

    vec2f64 position;

    core::Weak<wroc_pointer_constraint> active_constraint;

    void attach(wroc_pointer*);
};

// -----------------------------------------------------------------------------

enum class wroc_pointer_constraint_committed_state : u32
{
    position_hint = 1 << 0,
    region        = 1 << 1,
    region_unset  = 1 << 2,
};

struct wroc_pointer_constraint_state
{
    core::Flags<wroc_pointer_constraint_committed_state> committed;
    vec2f64 position_hint;
    region2i32 region;
};

enum class wroc_pointer_constraint_type : u32
{
    locked,
    confined,
};

struct wroc_pointer_constraint : wroc_surface_addon, wroc_surface_state_queue_base<wroc_pointer_constraint_state>
{
    static constexpr wroc_surface_role role = wroc_surface_role::none;

    wroc_resource resource;

    wroc_pointer_constraint_type type;
    core::Weak<wroc_seat_pointer> pointer;
    zwp_pointer_constraints_v1_lifetime lifetime;

    bool has_been_deactivated = false;

    virtual void commit(wroc_commit_id) final override;
    virtual void apply(wroc_commit_id) final override;

    void activate();
    void deactivate();

    ~wroc_pointer_constraint();
};

void wroc_update_pointer_constraint_state();

// -----------------------------------------------------------------------------

struct wroc_data_source : wroc_object
{
    std::vector<std::string> mime_types;
    core::Flags<wl_data_device_manager_dnd_action> dnd_actions;
    bool cancelled = false;

    wroc_resource resource;

    // Current target mime_type, used for feedback
    std::string target;

    ~wroc_data_source();
};

struct wroc_data_offer : wroc_object
{
    wroc_resource resource;

    core::Weak<wroc_data_source> source;
    core::Weak<wroc_data_device> device;

    wl_data_device_manager_dnd_action action = WL_DATA_DEVICE_MANAGER_DND_ACTION_NONE;
    std::string mime_type;

    ~wroc_data_offer();
};

struct wroc_data_device : wroc_object
{
    wroc_seat* seat;

    wroc_resource resource;

    ~wroc_data_device();
};

void wroc_data_manager_offer_selection(wl_client*);
void wroc_data_manager_update_drag(wroc_surface*);
void wroc_data_manager_finish_drag();

// -----------------------------------------------------------------------------

struct wroc_drag_icon : wroc_surface_addon
{
    static constexpr wroc_surface_role role = wroc_surface_role::drag_icon;

    virtual void commit(wroc_commit_id) final override {};
    virtual void apply(wroc_commit_id) final override {};
};

// -----------------------------------------------------------------------------

struct wroc_cursor_surface : wroc_surface_addon
{
    static constexpr wroc_surface_role role = wroc_surface_role::cursor;

    virtual void commit(wroc_commit_id) final override {};
    virtual void apply(wroc_commit_id) final override {};
};

struct wroc_cursor_shape
{
    core::Ref<gpu_image> image;
    vec2i32         hotspot;
};

struct wroc_cursor_texture
{
    gpu_image* image;
    vec2i32     hotspot;
};

struct wroc_cursor : wroc_object
{
    const char* theme;
    int size;

    core::EnumMap<wp_cursor_shape_device_v1_shape, core::Ref<wroc_surface>> shapes;
};

void wroc_cursor_create();
void wroc_cursor_set(wroc_cursor*, wl_client*, wroc_surface* cursor_surface, vec2i32 hotspot);
void wroc_cursor_set(wroc_cursor*, wl_client*, wp_cursor_shape_device_v1_shape);
wroc_surface* wroc_cursor_get_shape(wroc_cursor*, wp_cursor_shape_device_v1_shape);
wroc_surface* wroc_cursor_get_current(wroc_seat_pointer*, wroc_cursor*);

// -----------------------------------------------------------------------------

enum class wroc_render_option : u32 { };

struct wroc_renderer_frame_data
{
    gpu_array<struct wroc_shader_rect> rects;
};

struct wroc_renderer : wroc_object
{
    struct {
        int format_table;
        usz format_table_size;
        std::vector<u16> tranche_formats;
    } buffer_feedback;

    core::Flags<wroc_render_option> options;

    gpu_format output_format;
    gpu_format_modifier_set output_format_modifiers;

    core::Ref<gpu_shader> fragment;
    core::Ref<gpu_shader> vertex;

    std::vector<wroc_renderer_frame_data> available_frames;
    std::vector<struct wroc_shader_rect> rects_cpu;

    core::Ref<gpu_image> background;
    core::Ref<gpu_sampler> sampler;

    gpu_format_set shm_formats;
    gpu_format_set dmabuf_formats;

    bool vsync = true;

    struct {
        bool show_debug_cursor = false;
    } debug;

    bool fps_limit_enabled = false;
    i32  fps_limit = 120;

    u32 max_frames_in_flight = 2;
    u32 max_swapchain_images = 2;

    bool screenshot_queued = false;

    ~wroc_renderer();
};

core::Ref<wroc_renderer> wroc_renderer_create(core::Flags<wroc_render_option>);
void wroc_renderer_init_buffer_feedback(wroc_renderer*);
void wroc_render_frame(wroc_output*);
void wroc_screenshot(rect2f64 rect);

// -----------------------------------------------------------------------------

struct wroc_imgui_frame_data
{
    gpu_array<ImDrawIdx> indices;
    gpu_array<ImDrawVert> vertices;
};

struct wroc_imgui : wroc_object
{
    std::chrono::steady_clock::time_point last_frame = {};

    ImGuiContext* context;

    rect2f64 layout_rect;

    core::Ref<gpu_shader> fragment;
    core::Ref<gpu_shader> vertex;

    gpu_array<ImDrawIdx> indices;
    gpu_array<ImDrawVert> vertices;
    std::vector<wroc_imgui_frame_data> available_frames;

    core::Ref<gpu_image> font_image;

    bool wants_mouse;
    bool wants_keyboard;

    wp_cursor_shape_device_v1_shape cursor_shape;

    std::vector<std::move_only_function<void()>> on_render;
};

struct alignas(ImTextureID) wroc_imgui_texture
{
    gpu_image_handle<vec4f32> handle;

    wroc_imgui_texture(gpu_image* image, gpu_sampler* sampler)
        : handle(image, sampler)
    {}

    explicit operator ImTextureID() const { return std::bit_cast<ImTextureID>(*this); }
};

template<typename ...Args>
void ImGui_Text(std::format_string<Args...> fmt, Args&&... args)
{
    ImGui::TextUnformatted(std::vformat(fmt.get(), std::make_format_args(args...)).c_str());
}

void wroc_imgui_init();
void wroc_imgui_frame(wroc_imgui*, rect2f64 layout_rect);
void wroc_imgui_render(wroc_imgui*, gpu_commands*, rect2f64 viewport, vec2u32 framebuffer_extent);
bool wroc_imgui_handle_event(wroc_imgui*, const struct wroc_event&);

// -----------------------------------------------------------------------------

struct wroc_launcher;

void wroc_launcher_init();
void wroc_launcher_frame(wroc_launcher*, vec2f64 open_pos);
bool wroc_launcher_handle_event(wroc_launcher*, const struct wroc_event&);

// -----------------------------------------------------------------------------

struct wroc_debug_gui;
void wroc_debug_gui_init(bool show_on_startup);
void wroc_debug_gui_frame(wroc_debug_gui*);
bool wroc_debug_gui_handle_event(wroc_debug_gui*, const struct wroc_event&);

// -----------------------------------------------------------------------------

enum class wroc_interaction_mode : u32
{
    normal,
    move,
    size,
    focus_cycle,
    zone,
};

enum class wroc_edge : u32
{
    left   = 1 << 0,
    right  = 1 << 1,
    top    = 1 << 2,
    bottom = 1 << 3,
};

inline
core::Flags<wroc_edge> wroc_edges_inverse(core::Flags<wroc_edge> edges)
{
    core::Flags<wroc_edge> out = {};
    if      (edges.contains(wroc_edge::left))   out |= wroc_edge::right;
    else if (edges.contains(wroc_edge::right))  out |= wroc_edge::left;
    if      (edges.contains(wroc_edge::top))    out |= wroc_edge::bottom;
    else if (edges.contains(wroc_edge::bottom)) out |= wroc_edge::top;
    return out;
}

inline
vec2f64 wroc_edges_to_relative(core::Flags<wroc_edge> edges)
{
    vec2f64 rel{0.5, 0.5};
    if      (edges.contains(wroc_edge::left))   rel.x = 0;
    else if (edges.contains(wroc_edge::right))  rel.x = 1;
    if      (edges.contains(wroc_edge::top))    rel.y = 0;
    else if (edges.contains(wroc_edge::bottom)) rel.y = 1;
    return rel;
}

enum class wroc_direction : u32
{
    horizontal = 1 << 0,
    vertical   = 1 << 1,
};

struct wroc_server : wroc_object
{
    core::Ref<wroc_backend>  backend;
    gpu_context*      gpu;
    core::Ref<wroc_renderer> renderer;
    core::Ref<wroc_seat>     seat;

    core::Ref<wroc_imgui> imgui;
    core::Ref<wroc_debug_gui> debug_gui;
    core::Ref<wroc_launcher> launcher;

    wroc_modifier main_mod;
    u32 main_mod_evdev;

    std::chrono::steady_clock::time_point epoch;

    core::Ref<core::EventLoop> event_loop;

    u32 client_flushes_pending = 0;

    wl_display* display;
    std::string socket;
    std::string x11_socket;

    core::Ref<wroc_output_layout> output_layout;
    std::vector<wroc_surface*> surfaces;

    core::Weak<wroc_surface>  implicit_grab_surface;

    wroc_interaction_mode interaction_mode;

    // TODO: We should track these per client-pointer
    core::Ref<wroc_cursor> cursor;

    struct {
        bool noisy_frames = false;
    } debug;

    struct {
        core::Weak<wroc_toplevel> grabbed_toplevel;
        vec2f64 pointer_grab;
        vec2f64 surface_grab;
        vec2f64 relative;
    } movesize;

    struct {
        core::Weak<wroc_surface> cycled;
    } focus;

    struct {
        struct {
            vec2u32 zones = {6, 2};
            vec2i32 selection_leeway = {200, 200};
            i32 padding_inner = 8;
            struct {
                i32 left   = 9;
                i32 top    = 9;
                i32 right  = 9;
                i32 bottom = 9;
            } external_padding;
            vec4f32 color_initial = {0.6, 0.6, 0.6, 0.4};
            vec4f32 color_selected = {0.4, 0.4, 1.0, 0.6};
        } config;

        core::Weak<wroc_toplevel> toplevel;
        aabb2f64 initial_zone;
        aabb2f64 final_zone;
        bool selecting = false;
    } zone;

    struct {
        std::vector<wroc_data_device*> devices;
        std::vector<wroc_data_source*> sources;
        core::Weak<wroc_data_source> selection;

        struct {
            core::Weak<wroc_data_device> device;
            core::Weak<wroc_data_source> source;
            core::Weak<wroc_drag_icon>   icon;
            core::Weak<wroc_surface>     offered_surface;
            core::Weak<wroc_data_offer>  offer;
        } drag;
    } data_manager;
};

extern wroc_server* server;

wl_global* wroc_global(const wl_interface*, i32 version, wl_global_bind_func_t, void* data = nullptr);
#define WROC_GLOBAL(Interface, ...) \
    wroc_global(&Interface##_interface, wroc_##Interface##_version, wroc_##Interface##_bind_global __VA_OPT__(,) __VA_ARGS__)

u32 wroc_get_elapsed_milliseconds();
core::Flags<wroc_modifier> wroc_get_active_modifiers();

void wroc_begin_move_interaction(wroc_toplevel*, wroc_seat_pointer*, core::Flags<wroc_direction>);
void wroc_begin_resize_interaction(wroc_toplevel*, wroc_seat_pointer*, core::Flags<wroc_edge>);

wroc_surface* wroc_get_surface_under_cursor(wroc_toplevel** toplevel = nullptr);

struct wroc_spawn_env_action { const char* name; const char* value; };
struct wroc_spawn_x11_action { const char* x11_socket; bool force; };
struct wroc_spawn_cwd_action { const char* cwd; };
using wroc_spawn_action = std::variant<wroc_spawn_env_action, wroc_spawn_x11_action>;
pid_t wroc_spawn(std::string_view file, std::span<const std::string_view> argv, std::span<const wroc_spawn_action>);
void wroc_spawn(GAppInfo* app_info, std::span<const wroc_spawn_action>);

enum class wroc_setenv_option : u32
{
    // Imports the environment variable into the systemd environment
    system_wide = 1 << 0,
};

void wroc_setenv(const char* name, const char* value, core::Flags<wroc_setenv_option> options = {});
