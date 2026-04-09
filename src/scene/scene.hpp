#pragma once

#include "core/region.hpp"
#include "core/object.hpp"
#include "core/id.hpp"
#include "gpu/gpu.hpp"

#include "shader/render.h"

// -----------------------------------------------------------------------------

struct SceneNode;
struct SceneTree;
struct SceneTexture;
struct SceneInputRegion;

struct Scene;

auto scene_create(ExecContext*, Gpu*) -> Ref<Scene>;

enum class SceneLayer
{
    background,
    window,
    overlay,
};

auto scene_get_layer(Scene*, SceneLayer) -> SceneTree*;

// -----------------------------------------------------------------------------

void scene_push_io_event(Scene* scene, union IoEvent*);

// -----------------------------------------------------------------------------

void scene_render(Scene*, GpuImage* target, rect2f32 viewport);

// -----------------------------------------------------------------------------

struct SceneClient;

auto scene_client_create(Scene*) -> Ref<SceneClient>;

// -----------------------------------------------------------------------------

using SceneDamageListener = std::move_only_function<void()>;
void scene_add_damage_listener(Scene*, SceneDamageListener);

// -----------------------------------------------------------------------------

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
struct SceneSeat;

auto scene_input_device_get_type(    SceneInputDevice*) -> SceneInputDeviceType;
auto scene_input_device_get_pointer( SceneInputDevice*) -> ScenePointer*;
auto scene_input_device_get_keyboard(SceneInputDevice*) -> SceneKeyboard*;
auto scene_input_device_get_focus(   SceneInputDevice*) -> SceneInputRegion*;
auto scene_input_device_get_seat(    SceneInputDevice*)  -> SceneSeat*;

// -----------------------------------------------------------------------------

auto scene_get_seats(Scene*) -> std::span<SceneSeat* const>;

auto scene_seat_get_pointer( SceneSeat*) -> ScenePointer*;
auto scene_seat_get_keyboard(SceneSeat*) -> SceneKeyboard*;

auto scene_pointer_get_base( ScenePointer*)  -> SceneInputDevice*;
auto scene_keyboard_get_base(SceneKeyboard*) -> SceneInputDevice*;

auto scene_seat_get_modifiers(SceneSeat*, Flags<SceneModifierFlag> = {}) -> Flags<SceneModifier>;

// -----------------------------------------------------------------------------

void scene_pointer_focus(       ScenePointer*, SceneInputRegion*);
auto scene_pointer_get_position(ScenePointer*) -> vec2f32;
auto scene_pointer_get_pressed( ScenePointer*) -> std::span<const SceneScancode>;
auto scene_pointer_get_focus(   ScenePointer*) -> SceneInputRegion*;

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

void scene_keyboard_focus(        SceneKeyboard*, SceneInputRegion*);
auto scene_keyboard_get_modifiers(SceneKeyboard*, Flags<SceneModifierFlag> = {}) -> Flags<SceneModifier>;
auto scene_keyboard_get_pressed(  SceneKeyboard*) -> std::span<const SceneScancode>;
auto scene_keyboard_get_sym(      SceneKeyboard*, SceneScancode) -> xkb_keysym_t;
auto scene_keyboard_get_utf8(     SceneKeyboard*, SceneScancode) -> std::string;
auto scene_keyboard_get_info(     SceneKeyboard*) -> const SceneKeyboardInfo&;
auto scene_keyboard_get_focus(    SceneKeyboard*) -> SceneInputRegion*;

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

struct SceneNode
{
    SceneTree* parent;

    virtual ~SceneNode();

    virtual void damage(Scene*) = 0;
};

void scene_node_unparent(SceneNode*);
void scene_node_damage(  SceneNode*);

struct SceneTree : SceneNode
{
    Scene* scene;

    bool enabled;

    vec2f32 translation;

    struct {
        Uid   id;
        void* data;
    } userdata;

    std::vector<SceneNode*> children;

    virtual void damage(Scene*);

    ~SceneTree();
};

auto scene_tree_create() -> Ref<SceneTree>;

void scene_tree_set_enabled(SceneTree*, bool enabled);
void scene_tree_place_below(SceneTree*, SceneNode* sibling, SceneNode* to_place);
void scene_tree_place_above(SceneTree*, SceneNode* sibling, SceneNode* to_place);
void scene_tree_clear(      SceneTree*);

void scene_tree_set_translation(SceneTree*, vec2f32 translation);
auto scene_tree_get_position(   SceneTree*) -> vec2f32;

struct SceneTexture : SceneNode
{
    Ref<GpuImage>   image;
    Ref<GpuSampler> sampler;
    GpuBlendMode   blend;

    vec4u8   tint;
    aabb2f32 src;
    rect2f32 dst;

    virtual void damage(Scene*);

    ~SceneTexture();
};

auto scene_texture_create() -> Ref<SceneTexture>;
void scene_texture_set_image(SceneTexture*, GpuImage*, GpuSampler*, GpuBlendMode);
void scene_texture_set_tint( SceneTexture*, vec4u8   tint);
void scene_texture_set_src(  SceneTexture*, aabb2f32 src);
void scene_texture_set_dst(  SceneTexture*, rect2f32 dst);
void scene_texture_damage(   SceneTexture*, aabb2i32 damage);

struct SceneMeshSegment
{
    u32 vertex_offset;
    u32 first_index;
    u32 index_count;

    GpuBlendMode    blend;
    Ref<GpuImage>   image;
    Ref<GpuSampler> sampler;

    aabb2f32 clip;
};

struct SceneMesh : SceneNode
{
    vec2f32 offset;

    std::vector<SceneVertex>      vertices;
    std::vector<u16>              indices;
    std::vector<SceneMeshSegment> segments;

    virtual void damage(Scene*);

    ~SceneMesh();
};

auto scene_mesh_create() -> Ref<SceneMesh>;
void scene_mesh_update(SceneMesh*, std::span<const SceneVertex>      vertices,
                                   std::span<const u16>              indices,
                                   std::span<const SceneMeshSegment> segments,
                                   vec2f32 offset);

struct SceneInputRegion : SceneNode
{
    SceneClient* client;

    region2f32 region;

    virtual void damage(Scene*);

    ~SceneInputRegion();
};

auto scene_input_region_create(SceneClient*) -> Ref<SceneInputRegion>;
void scene_input_region_set_region(SceneInputRegion*, region2f32);

// -----------------------------------------------------------------------------

enum class SceneIterateDirection
{
    front_to_back,
    back_to_front,
};

enum class SceneIterateAction
{
    next, // Continue to next iteration action.
    skip, // Skip children. May only be returned from pre-visit, post-visit will be skipped.
    stop, // Stop iteration. If called in pre-visit, post-visit will be skipped.
};

static constexpr auto scene_iterate_default = [](auto*) {};

template<typename Visit>
auto scene_visit(SceneNode* node, Visit&& visit)
{
    if (auto* tree = dynamic_cast<SceneTree*>(node)) {
        return visit(tree);
    } else {
        return visit(node);
    }
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

enum class SceneEventType
{
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

    selection,
};

struct SceneKeyboardEvent
{
    SceneKeyboard* keyboard;
    union {
        struct {
            SceneScancode code;
            bool          pressed;
            bool          quiet;
        } key;
        SceneInputRegion* focus;
    };
};

struct ScenePointerEvent
{
    ScenePointer* pointer;
    union {
        struct {
            SceneScancode code;
            bool          pressed;
            bool          quiet;
        } button;
        struct {
            vec2f32 rel_accel;
            vec2f32 rel_unaccel;
        } motion;
        struct {
            vec2f32 delta;
        } scroll;
        SceneInputRegion* focus;
    };
};

struct SceneDataEvent
{
    SceneDataSource* source;
    SceneSeat*       seat;
};

struct SceneEvent
{
    SceneEventType type;

    union {
        SceneSeat*         seat;
        SceneKeyboardEvent keyboard;
        ScenePointerEvent  pointer;
        SceneDataEvent     data;
    };
};

using SceneEventHandlerFn = void(SceneEvent*);

void scene_client_set_event_handler(SceneClient*, std::move_only_function<SceneEventHandlerFn>&&);

enum class SceneEventFilterResult
{
    passthrough,
    capture,
};

struct SceneEventFilter;

auto scene_add_input_event_filter(Scene*, std::move_only_function<SceneEventFilterResult(SceneEvent*)>) -> Ref<SceneEventFilter>;
