#pragma once

#include "core/region.hpp"
#include "core/object.hpp"
#include "gpu/gpu.hpp"

#include "shader/render.h"

// -----------------------------------------------------------------------------

struct SceneOutput;
struct SceneNode;
struct SceneTree;
struct SceneTexture;
struct SceneInputRegion;
struct SceneWindow;

struct Scene;

auto scene_create(ExecContext*, Gpu*) -> Ref<Scene>;

enum class SceneLayer
{
    background,
    window,
    overlay,
};

auto scene_get_layer(Scene*, SceneLayer) -> SceneTree*;

// TODO: Requests should be handled per-output
void scene_request_frame(Scene*);

// -----------------------------------------------------------------------------

void scene_push_io_event(Scene* ctx, struct IoEvent*);

// -----------------------------------------------------------------------------

void scene_render(Scene*, GpuImage* target, rect2f32 viewport);

// -----------------------------------------------------------------------------

enum class SceneSystemId : u32 {};
auto scene_register_system(Scene*) -> SceneSystemId;

// -----------------------------------------------------------------------------

struct SceneClient;

auto scene_client_create(Scene*) -> Ref<SceneClient>;

// -----------------------------------------------------------------------------

enum class SceneOutputFlag
{
    // This output will act as a workarea for desktop layout and pointer constraints
    workspace = 1 << 0,
};

auto scene_output_create(SceneClient*, Flags<SceneOutputFlag>) -> Ref<SceneOutput>;
void scene_output_set_viewport(SceneOutput*, rect2f32 viewport);

auto scene_list_outputs(Scene*) -> std::span<SceneOutput* const>;
auto scene_output_get_viewport(SceneOutput*) -> rect2f32;

struct SceneFindOutputResult
{
    SceneOutput* output;
    vec2f32       position;
};
auto scene_find_output_for_point(Scene*, vec2f32 point) -> SceneFindOutputResult;

void scene_frame(Scene* ctx, SceneOutput*);

// -----------------------------------------------------------------------------

struct SceneFocus
{
    SceneClient*       client = nullptr;
    SceneInputRegion* region = nullptr;

    constexpr bool operator==(const SceneFocus&) const noexcept = default;
};

enum class SceneModifier : u32
{
    super = 1 << 0,
    shift = 1 << 1,
    ctrl  = 1 << 2,
    alt   = 1 << 3,
    num   = 1 << 4,
    caps  = 1 << 5,
};

enum class SceneModifierFlag
{
    ignore_locked = 1 << 0
};

using SceneScancode = u32;

// -----------------------------------------------------------------------------

enum class SceneInputDeviceType
{
    invalid,
    keyboard,
    pointer,
};

struct SceneInputDevice;
struct SceneKeyboard;
struct ScenePointer;

auto scene_input_device_get_type(    SceneInputDevice*) -> SceneInputDeviceType;
auto scene_input_device_get_pointer( SceneInputDevice*) -> ScenePointer*;
auto scene_input_device_get_keyboard(SceneInputDevice*) -> SceneKeyboard*;
auto scene_input_device_get_focus(   SceneInputDevice*) -> SceneFocus;

// -----------------------------------------------------------------------------

struct SceneSeat;

auto scene_get_seats(Scene*) -> std::span<SceneSeat* const>;

auto scene_seat_get_pointer( SceneSeat*) -> ScenePointer*;
auto scene_seat_get_keyboard(SceneSeat*) -> SceneKeyboard*;

auto scene_pointer_get_seat( ScenePointer*)  -> SceneSeat*;
auto scene_keyboard_get_seat(SceneKeyboard*) -> SceneSeat*;

auto scene_seat_get_modifiers(SceneSeat*, Flags<SceneModifierFlag> = {}) -> Flags<SceneModifier>;

// -----------------------------------------------------------------------------

void scene_pointer_focus(       ScenePointer*, SceneClient*, SceneInputRegion* = nullptr);
auto scene_pointer_get_position(ScenePointer*) -> vec2f32;
auto scene_pointer_get_pressed( ScenePointer*) -> std::span<const SceneScancode>;
auto scene_pointer_get_focus(   ScenePointer*) -> SceneFocus;

void scene_pointer_set_cursor( ScenePointer*, SceneNode*);
void scene_pointer_set_xcursor(ScenePointer*, const char* xcursor_semantic);

struct SceneKeyboardInfo
{
    xkb_context* context;
    xkb_state*   state;
    xkb_keymap*  keymap;
    i32          rate;
    i32          delay;
};

void scene_keyboard_clear_focus(  SceneKeyboard*);
auto scene_keyboard_get_modifiers(SceneKeyboard*, Flags<SceneModifierFlag> = {}) -> Flags<SceneModifier>;
auto scene_keyboard_get_pressed(  SceneKeyboard*) -> std::span<const SceneScancode>;
auto scene_keyboard_get_sym(      SceneKeyboard*, SceneScancode) -> xkb_keysym_t;
auto scene_keyboard_get_utf8(     SceneKeyboard*, SceneScancode) -> std::string;
auto scene_keyboard_get_info(     SceneKeyboard*) -> const SceneKeyboardInfo&;
auto scene_keyboard_get_focus(    SceneKeyboard*) -> SceneFocus;

// -----------------------------------------------------------------------------

using ScenePointerAccelFn = auto(vec2f32) -> vec2f32;

void scene_pointer_set_accel(ScenePointer*, std::move_only_function<ScenePointerAccelFn>&&);

// -----------------------------------------------------------------------------

struct SceneDataSource;

struct SceneDataSourceOps
{
    std::move_only_function<void()>                 cancel = [] {};
    std::move_only_function<void(const char*, int)> send;
};

auto scene_data_source_create(SceneClient*, SceneDataSourceOps&&) -> Ref<SceneDataSource>;

void scene_data_source_offer(      SceneDataSource*, const char* mime_type);
auto scene_data_source_get_offered(SceneDataSource*) -> std::span<const std::string>;

void scene_data_source_receive(SceneDataSource*, const char* mime_type, int fd);

void scene_seat_set_selection(SceneSeat*, SceneDataSource*);
auto scene_seat_get_selection(SceneSeat*) -> SceneDataSource*;

// -----------------------------------------------------------------------------

enum class SceneNodeType
{
    tree,
    texture,
    mesh,
    input_region,
};

struct SceneNode
{
    SceneNodeType type;

    SceneTree* parent;

    ~SceneNode();
};

void scene_node_unparent(SceneNode*);

struct SceneTree : SceneNode
{
    Scene* ctx;

    vec2f32 translation;

    bool enabled;

    SceneSystemId system;
    void*           userdata;

    RefVector<SceneNode> children;

    ~SceneTree();
};

auto scene_tree_create(Scene*) -> Ref<SceneTree>;

void scene_tree_set_enabled(SceneTree*, bool enabled);
void scene_tree_place_below(SceneTree*, SceneNode* sibling, SceneNode* to_place);
void scene_tree_place_above(SceneTree*, SceneNode* sibling, SceneNode* to_place);
void scene_tree_clear(      SceneTree*);

void scene_tree_set_translation(SceneTree*, vec2f32 translation);

inline
auto scene_tree_get_position(SceneTree* tree) -> vec2f32
{
    return tree->translation + (tree->parent ? scene_tree_get_position(tree->parent) : vec2f32{});
}

struct SceneTexture : SceneNode
{
    Ref<GpuImage>   image;
    Ref<GpuSampler> sampler;
    GpuBlendMode   blend;

    vec4u8   tint;
    aabb2f32 src;
    rect2f32 dst;
};

auto scene_texture_create(Scene*) -> Ref<SceneTexture>;
void scene_texture_set_image(SceneTexture*, GpuImage*, GpuSampler*, GpuBlendMode);
void scene_texture_set_tint( SceneTexture*, vec4u8   tint);
void scene_texture_set_src(  SceneTexture*, aabb2f32 src);
void scene_texture_set_dst(  SceneTexture*, rect2f32 dst);
void scene_texture_damage(   SceneTexture*, aabb2i32 damage);

struct SceneMesh : SceneNode
{
    Ref<GpuImage>   image;
    Ref<GpuSampler> sampler;
    GpuBlendMode    blend;

    aabb2f32 clip;

    std::vector<SceneVertex> vertices;
    std::vector<u16>          indices;
};

auto scene_mesh_create(Scene*) -> Ref<SceneMesh>;
void scene_mesh_update(SceneMesh*, GpuImage*, GpuSampler*, GpuBlendMode, aabb2f32 clip, std::span<const SceneVertex> vertices, std::span<const u16> indices);

struct SceneInputRegion : SceneNode
{
    SceneClient* client;
    Weak<SceneWindow> window;

    region2f32 region;

    ~SceneInputRegion();
};

auto scene_input_region_create(SceneClient*, SceneWindow*) -> Ref<SceneInputRegion>;
void scene_input_region_set_region(SceneInputRegion*, region2f32);

// -----------------------------------------------------------------------------

// Represents a normal interactable "toplevel" window.
struct SceneWindow;

auto scene_window_create(SceneClient*) -> Ref<SceneWindow>;

void scene_window_set_title(SceneWindow*, std::string_view title);

void scene_window_map(  SceneWindow*);
void scene_window_unmap(SceneWindow*);
void scene_window_raise(SceneWindow*);

auto scene_window_get_tree(SceneWindow*) -> SceneTree*;

void scene_window_request_reposition(SceneWindow*, rect2f32 frame, vec2f32 gravity);
void scene_window_request_close(     SceneWindow*);

void scene_window_set_frame(SceneWindow*, rect2f32 frame);
auto scene_window_get_frame(SceneWindow*) -> rect2f32;

auto scene_find_window_at(Scene*, vec2f32 point) -> SceneWindow*;

// -----------------------------------------------------------------------------

enum class SceneIterateAction
{
    next, // Continue to next iteration action.
    skip, // Skip children. May only be returned from pre-visit, post-visit will be skipped.
    stop, // Stop iteration. If called in pre-visit, post-visit will be skipped.
};

static constexpr auto scene_iterate_default = [](auto*) {};

enum class SceneIterateDirection
{
    front_to_back,
    back_to_front,
};

template<typename Visit>
auto scene_visit(SceneNode* node, Visit&& visit)
{
    switch (node->type) {
        break;case SceneNodeType::texture:
            return visit(static_cast<SceneTexture*>(node));
        break;case SceneNodeType::mesh:
            return visit(static_cast<SceneMesh*>(node));
        break;case SceneNodeType::input_region:
            return visit(static_cast<SceneInputRegion*>(node));
        break;case SceneNodeType::tree:
            return visit(static_cast<SceneTree*>(node));
    }

    debug_unreachable();
}

template<SceneIterateDirection Dir, typename Pre, typename Leaf, typename Post>
auto scene_iterate(SceneTree* tree, Pre&& pre, Leaf&& leaf, Post&& post) -> SceneIterateAction
{
    static constexpr auto call = [](auto fn, auto* arg) {
        if constexpr (std::same_as<decltype(fn(arg)), SceneIterateAction>) {
            return fn(arg);
        } else {
            fn(arg);
            return SceneIterateAction::next;
        }
    };

    static constexpr auto is_defaulted = []<typename Fn>(Fn&&) {
        return std::same_as<std::remove_cvref_t<Fn>, std::remove_cvref_t<decltype(scene_iterate_default)>>;
    };

    SceneIterateAction pre_action;
    if constexpr (is_defaulted(pre)) {
        pre_action = tree->enabled
            ? SceneIterateAction::next
            : SceneIterateAction::skip;
    } else {
        pre_action = call(pre, tree);
    }

    if (pre_action == SceneIterateAction::stop) return SceneIterateAction::stop;
    if (pre_action == SceneIterateAction::skip) return SceneIterateAction::next;

    auto for_each = [&](auto&& children) -> SceneIterateAction {
        for (auto* child : children) {
            auto action = scene_visit(child, OverloadSet {
                [&](SceneTree* tree) {
                    return scene_iterate<Dir>(static_cast<SceneTree*>(child),
                        std::forward<Pre>(pre),
                        std::forward<Leaf>(leaf),
                        std::forward<Post>(post));
                },
                [&](auto* node) { return call(leaf, node); },
            });
            if (action != SceneIterateAction::next) return action;
        }
        return SceneIterateAction::next;
    };

    SceneIterateAction action;
    switch (Dir) {
        break;case SceneIterateDirection::front_to_back: action = for_each(tree->children | std::views::reverse);
        break;case SceneIterateDirection::back_to_front: action = for_each(tree->children);
    }

    if (action == SceneIterateAction::stop) return action;

    return call(post, tree);
}

// -----------------------------------------------------------------------------

struct SceneHotkey
{
    Flags<SceneModifier> mod;
    SceneScancode        code;

    constexpr bool operator==(const SceneHotkey&) const noexcept = default;
};

MAKE_STRUCT_HASHABLE(SceneHotkey, v.mod, v.code)

auto scene_client_hotkey_register(  SceneClient*, SceneHotkey) -> bool;
void scene_client_hotkey_unregister(SceneClient*, SceneHotkey);

// -----------------------------------------------------------------------------

enum class SceneEventType
{
    hotkey,

    seat_add,
    seat_configure,
    seat_remove,

    keyboard_enter,
    keyboard_leave,
    keyboard_key,
    keyboard_modifier,

    pointer_enter,
    pointer_leave,
    pointer_motion,
    pointer_button,
    pointer_scroll,

    window_reposition,
    window_close,

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

struct SceneHotkeyEvent
{
    SceneInputDevice* input_device;

    SceneHotkey hotkey;
    bool         pressed;
};

struct SceneKeyboardEvent
{
    SceneKeyboard* keyboard;
    union {
        struct {
            SceneScancode code;
            bool           pressed;
            bool           quiet;
        } key;
        struct {
            SceneInputRegion* region;
        } focus;
    };
};

struct ScenePointerEvent
{
    ScenePointer* pointer;
    union {
        struct {
            SceneScancode code;
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
            SceneInputRegion* region;
        } focus;
    };
};

struct SceneWindowEvent
{
    SceneWindow* window;
    union {
        struct {
            rect2f32 frame;
            vec2f32  gravity;
        } reposition;
    };
};

struct SceneRedrawEvent
{
    SceneOutput* output;
};

struct SceneDataEvent
{
    SceneDataSource* source;
    SceneSeat*        seat;
};

struct SceneEvent
{
    SceneEventType type;

    union {
        SceneSeat*          seat;
        SceneHotkeyEvent   hotkey;
        SceneWindowEvent   window;
        SceneKeyboardEvent keyboard;
        ScenePointerEvent  pointer;
        SceneRedrawEvent   redraw;
        SceneOutput*        output;
        SceneDataEvent     data;
    };
};

using SceneEventHandlerFn = void(SceneEvent*);

void scene_client_set_event_handler(SceneClient*, std::move_only_function<SceneEventHandlerFn>&&);
