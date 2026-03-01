#pragma once

#include "core/region.hpp"
#include "core/object.hpp"
#include "gpu/gpu.hpp"

#include "render.h"

// -----------------------------------------------------------------------------

struct scene_node;
struct scene_tree;
struct scene_transform;
struct scene_texture;
struct scene_input_region;

struct scene_context;
CORE_OBJECT_EXPLICIT_DECLARE(scene_context);

auto scene_create(gpu_context*, struct io_context*) -> ref<scene_context>;

struct scene_scene
{
    scene_tree*      tree;
    scene_transform* transform;
};

enum class scene_layer
{
    background,
    window,
    overlay,
};

auto scene_get_layer(scene_context*, scene_layer) -> scene_tree*;
auto scene_get_root_transform(scene_context*) -> scene_transform*;
void scene_request_redraw(scene_context*);

// -----------------------------------------------------------------------------

struct scene_output;

auto scene_list_outputs(scene_context*) -> std::span<scene_output* const>;
auto scene_output_get_viewport(scene_output*) -> rect2f32;

struct scene_find_output_result
{
    scene_output* output;
    vec2f32       position;
};
auto scene_find_output_for_point(scene_context*, vec2f32 point) -> scene_find_output_result;

// -----------------------------------------------------------------------------

struct scene_client;
CORE_OBJECT_EXPLICIT_DECLARE(scene_client);

auto scene_client_create(scene_context*) -> ref<scene_client>;

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

using scene_scancode = u32;

struct scene_keyboard;
struct scene_pointer;

auto scene_pointer_get_position(scene_context*) -> vec2f32;
void scene_pointer_grab(scene_client*);
void scene_pointer_ungrab(scene_client*);

auto scene_pointer_get_pressed(scene_context*) -> std::span<const scene_scancode>;

auto scene_keyboard_get_modifiers(scene_context*) -> flags<scene_modifier>;
void scene_keyboard_grab(scene_client*);
// Clear client keyboard grab, defers to next most recent keyboard grab
void scene_keyboard_ungrab(scene_client*);
// Clear all keyboard grabs
void scene_keyboard_clear_focus(scene_context*);

auto scene_keyboard_get_pressed(scene_context*) -> std::span<const scene_scancode>;

auto scene_keyboard_get_sym( scene_context*, scene_scancode) -> xkb_keysym_t;
auto scene_keyboard_get_utf8(scene_context*, scene_scancode) -> std::string;

struct scene_keyboard_info
{
    xkb_context* context;
    xkb_state*   state;
    xkb_keymap*  keymap;
    i32          rate;
    i32          delay;
};

auto scene_keyboard_get_info(scene_context*) -> const scene_keyboard_info&;

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

void scene_pointer_set_driver(scene_context*, std::move_only_function<scene_pointer_driver_fn>&&);

// -----------------------------------------------------------------------------

enum class scene_node_type
{
    transform,
    tree,
    texture,
    mesh,
    input_region,
};

struct scene_node : core_object
{
    scene_node_type      type;      // Node type
    scene_tree*          parent;    // Parent in the layer hierarchy, controls z-order and visibility
    ref<scene_transform> transform; // Parent in the transform hierarhcy, controls xy positioning

    ~scene_node();
};

void scene_node_unparent(     scene_node*);
void scene_node_set_transform(scene_node*, scene_transform*);

struct scene_transform_state
{
    vec2f32 translation;
    f32     scale;

    auto to_global(vec2f32 local) -> vec2f32
    {
        return local * scale + translation;
    }

    auto to_local(vec2f32 global) -> vec2f32
    {
        return (global - translation) / scale;
    }
};

struct scene_transform : scene_node
{
    scene_transform_state local;
    scene_transform_state global; // TODO: Move this to `scene_node` and drop root transform?

    std::vector<scene_node*> children;

    ~scene_transform();
};

auto scene_transform_create(scene_context*) -> ref<scene_transform>;
void scene_transform_update(scene_transform*, vec2f32 translation, f32 scale);
auto scene_transform_get_local( scene_transform*) -> scene_transform_state;
auto scene_transform_get_global(scene_transform*) -> scene_transform_state;

struct scene_tree : scene_node
{
    scene_context* ctx;

    bool enabled;

    core_object* userdata;

    core_ref_vector<scene_node> children;

    ~scene_tree();
};

auto scene_tree_create(scene_context*) -> ref<scene_tree>;
void scene_tree_set_enabled(scene_tree*, bool enabled);
void scene_tree_place_below(scene_tree*, scene_node* reference, scene_node* to_place);
void scene_tree_place_above(scene_tree*, scene_node* reference, scene_node* to_place);

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
void scene_texture_set_dst(  scene_texture*, aabb2f32 dst);
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

auto scene_window_get_tree(     scene_window*) -> scene_tree*;
auto scene_window_get_transform(scene_window*) -> scene_transform*;

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

auto scene_client_hotkey_register(      scene_client*, scene_hotkey) -> bool;
void scene_client_hotkey_unregister(    scene_client*, scene_hotkey);
void scene_client_hotkey_unregister_all(scene_client*);

// -----------------------------------------------------------------------------

enum class scene_event_type
{
    hotkey,

    keyboard_key,
    keyboard_modifier,

    pointer_motion,
    pointer_button,
    pointer_scroll,

    focus_keyboard,
    focus_pointer,

    // Requests that a client adjust its position/size as requested.
    // This request does not need to be honoured, clients may update
    // their window frames at any time for any reason.
    window_reposition,

    // Sent before a frame may be composited to an output.
    // This may be sent even if there is no new scene graph changes
    // to commit, in response to a scene frame request.
    // Scene graph changes made directly in response to this event
    // will be applied immediately.
    redraw,

    // Sent when an output is added/removed/moved in the scene output layout.
    output_layout,
};

struct scene_hotkey_event
{
    scene_hotkey hotkey;
    bool         pressed;
};

struct scene_key_event
{
    scene_scancode code;
    bool           pressed;
    bool           quiet;
};

struct scene_pointer_event
{
    union {
        struct {
            vec2f32 rel_accel;
            vec2f32 rel_unaccel;
        } motion;
        struct {
            vec2f32 delta;
        } scroll;
    };
};

struct scene_focus
{
    scene_client*       client;
    scene_input_region* region;
};

struct scene_focus_event
{
    scene_focus lost;
    scene_focus gained;
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

struct scene_event
{
    scene_event_type type;

    union {
        scene_hotkey_event  hotkey;
        scene_window_event  window;
        scene_key_event     key;
        scene_pointer_event pointer;
        scene_focus_event   focus;
        scene_redraw_event  redraw;
    };
};

using scene_event_handler_fn = void(scene_event*);

void scene_client_set_event_handler(scene_client*, std::move_only_function<scene_event_handler_fn>&&);
