#pragma once

#include "core/region.hpp"
#include "core/object.hpp"
#include "gpu/gpu.hpp"

#include "render.h"

// -----------------------------------------------------------------------------

struct scene_output;
struct scene_node;
struct scene_tree;
struct scene_texture;
struct scene_input_region;

struct scene_context;
CORE_OBJECT_EXPLICIT_DECLARE(scene_context);

auto scene_create(gpu_context*, struct io_context*) -> ref<scene_context>;

enum class scene_layer
{
    background,
    window,
    overlay,
};

auto scene_get_layer(scene_context*, scene_layer) -> scene_tree*;

// TODO: Requests should be handled per-output
void scene_request_frame(scene_context*);

// -----------------------------------------------------------------------------

void scene_push_io_event(scene_context* ctx, struct io_event*);

// -----------------------------------------------------------------------------

auto scene_render(scene_context* ctx, gpu_image* target, rect2f32 viewport) -> gpu_syncpoint;

// -----------------------------------------------------------------------------

struct scene_client;
CORE_OBJECT_EXPLICIT_DECLARE(scene_client);

auto scene_client_create(scene_context*) -> ref<scene_client>;

// -----------------------------------------------------------------------------

CORE_OBJECT_EXPLICIT_DECLARE(scene_output);

auto scene_output_create(scene_client*) -> ref<scene_output>;
void scene_output_set_viewport(scene_output*, rect2f32 viewport);

auto scene_list_outputs(scene_context*) -> std::span<scene_output* const>;
auto scene_output_get_viewport(scene_output*) -> rect2f32;

struct scene_find_output_result
{
    scene_output* output;
    vec2f32       position;
};
auto scene_find_output_for_point(scene_context*, vec2f32 point) -> scene_find_output_result;

void scene_frame(scene_context* ctx, scene_output*, struct io_output* output, gpu_image_pool* pool);

// -----------------------------------------------------------------------------

enum class scene_modifier : u32
{
    super = 1 << 0,
    shift = 1 << 1,
    ctrl  = 1 << 2,
    alt   = 1 << 3,
    num   = 1 << 4,
    caps  = 1 << 5,
};

enum class scene_modifier_flags
{
    ignore_locked = 1 << 0
};

using scene_scancode = u32;

auto scene_get_modifiers(scene_context*, flags<scene_modifier_flags> = {}) -> flags<scene_modifier>;

enum class scene_input_device_type
{
    invalid,
    keyboard,
    pointer,
};

struct scene_input_device;
struct scene_keyboard;
struct scene_pointer;

auto scene_input_device_get_type(    scene_input_device*) -> scene_input_device_type;
auto scene_input_device_get_pointer( scene_input_device*) -> scene_pointer*;
auto scene_input_device_get_keyboard(scene_input_device*) -> scene_keyboard*;

auto scene_get_pointer( scene_context*) -> scene_pointer*;
auto scene_get_keyboard(scene_context*) -> scene_keyboard*;

void scene_pointer_focus(       scene_pointer*, scene_client*, scene_input_region* = nullptr);
auto scene_pointer_get_position(scene_pointer*) -> vec2f32;
auto scene_pointer_get_pressed( scene_pointer*) -> std::span<const scene_scancode>;

struct scene_keyboard_info
{
    xkb_context* context;
    xkb_state*   state;
    xkb_keymap*  keymap;
    i32          rate;
    i32          delay;
};

void scene_keyboard_clear_focus(  scene_keyboard*);
auto scene_keyboard_get_modifiers(scene_keyboard*, flags<scene_modifier_flags> = {}) -> flags<scene_modifier>;
auto scene_keyboard_get_pressed(  scene_keyboard*) -> std::span<const scene_scancode>;
auto scene_keyboard_get_sym(      scene_keyboard*, scene_scancode) -> xkb_keysym_t;
auto scene_keyboard_get_utf8(     scene_keyboard*, scene_scancode) -> std::string;
auto scene_keyboard_get_info(     scene_keyboard*) -> const scene_keyboard_info&;

// -----------------------------------------------------------------------------

struct scene_pointer_driver_in
{
    vec2f32 position;
    vec2f32 delta;
};

struct scene_pointer_driver_out
{
    vec2f32 position;
    vec2f32 accel;
    vec2f32 unaccel;
};

using scene_pointer_driver_fn = auto(scene_pointer_driver_in) -> scene_pointer_driver_out;

void scene_pointer_set_driver(scene_pointer*, std::move_only_function<scene_pointer_driver_fn>&&);

// -----------------------------------------------------------------------------

struct scene_data_source;
CORE_OBJECT_EXPLICIT_DECLARE(scene_data_source);

struct scene_data_source_ops
{
    std::move_only_function<void()>                 cancel = [] {};
    std::move_only_function<void(const char*, int)> send;
};

auto scene_data_source_create(scene_client*, scene_data_source_ops&&) -> ref<scene_data_source>;

void scene_data_source_offer(      scene_data_source*, const char* mime_type);
auto scene_data_source_get_offered(scene_data_source*) -> std::span<const std::string>;

void scene_data_source_send(scene_data_source*, const char* mime_type, int fd);

void scene_set_selection(scene_context*, scene_data_source*);
auto scene_get_selection(scene_context*) -> scene_data_source*;

// -----------------------------------------------------------------------------

enum class scene_node_type
{
    tree,
    texture,
    mesh,
    input_region,
};

struct scene_node : core_object
{
    scene_node_type type;

    scene_tree* parent;

    ~scene_node();
};

void scene_node_unparent(scene_node*);

struct scene_tree : scene_node
{
    scene_context* ctx;

    vec2f32 translation;

    bool enabled;

    core_object* userdata;

    core_ref_vector<scene_node> children;

    ~scene_tree();
};

auto scene_tree_create(scene_context*) -> ref<scene_tree>;

void scene_tree_set_enabled(scene_tree*, bool enabled);
void scene_tree_place_below(scene_tree*, scene_node* reference, scene_node* to_place);
void scene_tree_place_above(scene_tree*, scene_node* reference, scene_node* to_place);

void scene_tree_set_translation(scene_tree*, vec2f32 translation);

inline
auto scene_tree_get_position(scene_tree* tree) -> vec2f32
{
    return tree->translation + (tree->parent ? scene_tree_get_position(tree->parent) : vec2f32{});
}

struct scene_texture : scene_node
{
    ref<gpu_image>   image;
    ref<gpu_sampler> sampler;
    gpu_blend_mode   blend;

    vec4u8   tint;
    aabb2f32 src;
    rect2f32 dst;
};

auto scene_texture_create(scene_context*) -> ref<scene_texture>;
void scene_texture_set_image(scene_texture*, gpu_image*, gpu_sampler*, gpu_blend_mode);
void scene_texture_set_tint( scene_texture*, vec4u8   tint);
void scene_texture_set_src(  scene_texture*, aabb2f32 src);
void scene_texture_set_dst(  scene_texture*, rect2f32 dst);
void scene_texture_damage(   scene_texture*, aabb2f32 damage);

struct scene_mesh : scene_node
{
    ref<gpu_image>   image;
    ref<gpu_sampler> sampler;
    gpu_blend_mode   blend;

    aabb2f32 clip;

    std::vector<scene_vertex> vertices;
    std::vector<u16>          indices;
};

auto scene_mesh_create(scene_context*) -> ref<scene_mesh>;
void scene_mesh_update(scene_mesh*, gpu_image*, gpu_sampler*, gpu_blend_mode, aabb2f32 clip, std::span<const scene_vertex> vertices, std::span<const u16> indices);

struct scene_input_region : scene_node
{
    scene_client* client;

    region2f32 region;

    ~scene_input_region();
};

auto scene_input_region_create(scene_client*) -> ref<scene_input_region>;
void scene_input_region_set_region(scene_input_region*, region2f32);

// -----------------------------------------------------------------------------

// Represents a normal interactable "toplevel" window.
struct scene_window;
CORE_OBJECT_EXPLICIT_DECLARE(scene_window);

auto scene_window_create(scene_client*) -> ref<scene_window>;

void scene_window_set_title(scene_window*, std::string_view title);

void scene_window_map(  scene_window*);
void scene_window_unmap(scene_window*);
void scene_window_raise(scene_window*);

auto scene_window_get_tree(scene_window*) -> scene_tree*;

void scene_window_request_reposition(scene_window*, rect2f32 frame, vec2f32 gravity);
void scene_window_set_frame(scene_window*, rect2f32 frame);
auto scene_window_get_frame(scene_window*) -> rect2f32;

auto scene_find_window_at(scene_context*, vec2f32 point) -> scene_window*;

// -----------------------------------------------------------------------------

enum class scene_iterate_action
{
    next, // Continue to next iteration action.
    skip, // Skip children.
    stop, // Stop iteration. If called in pre-visit, post-visit will be skipped.
};

static constexpr auto scene_iterate_default = [](auto*) -> scene_iterate_action { return scene_iterate_action::next; };

enum class scene_iterate_direction
{
    front_to_back,
    back_to_front,
};

template<typename Pre, typename Leaf, typename Post>
auto scene_iterate(scene_tree* tree, scene_iterate_direction dir, Pre&& pre, Leaf&& leaf, Post&& post) -> scene_iterate_action
{
    if (!tree->enabled) return scene_iterate_action::next;

    auto pre_action = pre(tree);
    if (pre_action == scene_iterate_action::stop) return scene_iterate_action::stop;
    if (pre_action == scene_iterate_action::skip) return scene_iterate_action::next;

    auto for_each = [&](auto&& children) -> scene_iterate_action {
        for (auto* child : children) {
            if (child->type == scene_node_type::tree) {
                if (scene_iterate(static_cast<scene_tree*>(child), dir,
                        std::forward<Pre>(pre), std::forward<Leaf>(leaf), std::forward<Post>(post))
                            == scene_iterate_action::stop) {
                    return scene_iterate_action::stop;
                }
            } else {
                if (leaf(child) == scene_iterate_action::stop) return scene_iterate_action::stop;
            }
        }
        return scene_iterate_action::next;
    };

    auto action = dir == scene_iterate_direction::front_to_back
        ? for_each(tree->children | std::views::reverse)
        : for_each(tree->children);
    if (action == scene_iterate_action::stop) return action;

    return post(tree);
}

// -----------------------------------------------------------------------------

struct scene_hotkey
{
    flags<scene_modifier> mod;
    scene_scancode        code;

    constexpr bool operator==(const scene_hotkey&) const noexcept = default;
};

CORE_MAKE_STRUCT_HASHABLE(scene_hotkey, v.mod, v.code)

auto scene_client_hotkey_register(  scene_client*, scene_hotkey) -> bool;
void scene_client_hotkey_unregister(scene_client*, scene_hotkey);

// -----------------------------------------------------------------------------

enum class scene_event_type
{
    hotkey,

    keyboard_enter,
    keyboard_leave,
    keyboard_key,
    keyboard_modifier,

    pointer_enter,
    pointer_leave,
    pointer_motion,
    pointer_button,
    pointer_scroll,

    // Requests that a client adjust its position/size as requested.
    // This request does not need to be honoured, clients may update
    // their window frames at any time for any reason.
    window_reposition,

    output_added,
    output_configured,
    output_removed,
    output_layout,

    // Requests the output owner to make a `scene_frame` call at the
    // next time that the output would accept a content update.
    output_frame_request,

    // Sent before a frame may be composited to an output.
    // This may be sent even if there is no new scene graph changes
    // to commit, in response to a scene frame request.
    // Scene graph changes made directly in response to this event
    // will be applied immediately.
    output_frame,

    selection,
};

struct scene_hotkey_event
{
    scene_input_device* input_device;

    scene_hotkey hotkey;
    bool         pressed;
};

struct scene_keyboard_event
{
    scene_keyboard* keyboard;
    union {
        struct {
            scene_scancode code;
            bool           pressed;
            bool           quiet;
        } key;
        struct {
            scene_input_region* region;
        } focus;
    };
};

struct scene_pointer_event
{
    scene_pointer* pointer;
    union {
        struct {
            scene_scancode code;
            bool           pressed;
            bool           quiet;
        } button;
        struct {
            vec2f32 rel_accel;
            vec2f32 rel_unaccel;
        } motion;
        struct {
            vec2f32 delta;
        } scroll;
        struct {
            scene_input_region* region;
        } focus;
    };
};

struct scene_window_event
{
    scene_window* window;
    union {
        struct {
            rect2f32 frame;
            vec2f32  gravity;
        } reposition;
    };
};

struct scene_redraw_event
{
    scene_output* output;
};

struct scene_data_event
{
    scene_data_source* source;
};

struct scene_event
{
    scene_event_type type;

    union {
        scene_hotkey_event   hotkey;
        scene_window_event   window;
        scene_keyboard_event keyboard;
        scene_pointer_event  pointer;
        scene_redraw_event   redraw;
        scene_output*        output;
        scene_data_event     data;
    };
};

using scene_event_handler_fn = void(scene_event*);

void scene_client_set_event_handler(scene_client*, std::move_only_function<scene_event_handler_fn>&&);
