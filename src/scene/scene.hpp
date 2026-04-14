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
struct SeatInputRegion;

struct Scene;

auto scene_create(Gpu*) -> Ref<Scene>;

auto scene_get_root(Scene*) -> SceneTree*;

// -----------------------------------------------------------------------------

void scene_render(Scene*, GpuImage* target, rect2f32 viewport);

// -----------------------------------------------------------------------------

struct SeatClient;

auto seat_client_create() -> Ref<SeatClient>;

// -----------------------------------------------------------------------------

using SceneDamageListener = std::move_only_function<void(SceneNode*)>;
void scene_add_damage_listener(Scene*, SceneDamageListener);

// -----------------------------------------------------------------------------

struct SeatCursorManager;

auto scene_cursor_manager_create(Gpu*, const char* theme, i32 size) -> Ref<SeatCursorManager>;

// -----------------------------------------------------------------------------

enum class SeatModifier : u32
{
    super = 1 << 0,
    shift = 1 << 1,
    ctrl  = 1 << 2,
    alt   = 1 << 3,
    num   = 1 << 4,
    caps  = 1 << 5,
};

enum class SeatModifierFlag
{
    ignore_locked = 1 << 0
};

using SeatInputCode = u32;

// -----------------------------------------------------------------------------

struct SeatKeyboard;
struct SeatPointer;
struct Seat;

// -----------------------------------------------------------------------------

auto seat_create(SeatCursorManager*, SceneTree* pointer_root, SceneTree* pointer_layer) -> Ref<Seat>;

void seat_push_io_event(Seat*, union IoEvent*);

auto seat_get_pointer( Seat*) -> SeatPointer*;
auto seat_get_keyboard(Seat*) -> SeatKeyboard*;

auto seat_get_modifiers(Seat*, Flags<SeatModifierFlag> = {}) -> Flags<SeatModifier>;

// -----------------------------------------------------------------------------

void seat_pointer_focus(       SeatPointer*, SeatInputRegion*);
auto seat_pointer_get_position(SeatPointer*) -> vec2f32;
auto seat_pointer_get_pressed( SeatPointer*) -> std::span<const SeatInputCode>;
auto seat_pointer_get_focus(   SeatPointer*) -> SeatInputRegion*;
auto seat_pointer_get_seat(    SeatPointer*) -> Seat*;

void seat_update_pointers(Seat*);

void seat_pointer_set_cursor( SeatPointer*, SceneNode*);
void seat_pointer_set_xcursor(SeatPointer*, const char* xcursor_semantic);

struct SeatKeyboardInfo
{
    xkb_context* context;
    xkb_state*   state;
    xkb_keymap*  keymap;
    i32          rate;
    i32          delay;
};

void seat_keyboard_focus(        SeatKeyboard*, SeatInputRegion*);
auto seat_keyboard_get_modifiers(SeatKeyboard*, Flags<SeatModifierFlag> = {}) -> Flags<SeatModifier>;
auto seat_keyboard_get_pressed(  SeatKeyboard*) -> std::span<const SeatInputCode>;
auto seat_keyboard_get_sym(      SeatKeyboard*, SeatInputCode) -> xkb_keysym_t;
auto seat_keyboard_get_utf8(     SeatKeyboard*, SeatInputCode) -> std::string;
auto seat_keyboard_get_info(     SeatKeyboard*) -> const SeatKeyboardInfo&;
auto seat_keyboard_get_focus(    SeatKeyboard*) -> SeatInputRegion*;
auto seat_keyboard_get_seat(     SeatKeyboard*) -> Seat*;

// -----------------------------------------------------------------------------

using SeatPointerAccelFn = auto(vec2f32) -> vec2f32;

void seat_pointer_set_accel(SeatPointer*, std::move_only_function<SeatPointerAccelFn>&&);

// -----------------------------------------------------------------------------

struct SeatDataSource;

struct SeatDataSourceOps
{
    std::move_only_function<void()>                 cancel = [] {};
    std::move_only_function<void(const char*, int)> send;
};

auto seat_data_source_create(SeatClient*, SeatDataSourceOps&&) -> Ref<SeatDataSource>;

void seat_data_source_offer(      SeatDataSource*, const char* mime_type);
auto seat_data_source_get_offered(SeatDataSource*) -> std::span<const std::string>;

void seat_data_source_receive(SeatDataSource*, const char* mime_type, int fd);

void seat_set_selection(Seat*, SeatDataSource*);
auto seat_get_selection(Seat*) -> SeatDataSource*;

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

struct SeatInputRegion : SceneNode
{
    SeatClient* client;

    region2f32 region;

    virtual void damage(Scene*);

    ~SeatInputRegion();
};

auto scene_input_region_create(SeatClient*) -> Ref<SeatInputRegion>;
void scene_input_region_set_region(SeatInputRegion*, region2f32);

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

enum class SeatEventType
{
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

struct SeatKeyboardEvent
{
    SeatEventType type;
    SeatKeyboard* keyboard;
    union {
        struct {
            SeatInputCode code;
            bool          pressed;
            bool          quiet;
        } key;
        SeatInputRegion* focus;
    };
};

struct SeatPointerEvent
{
    SeatEventType type;
    SeatPointer* pointer;
    union {
        struct {
            SeatInputCode code;
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
        SeatInputRegion* focus;
    };
};

struct SeatDataEvent
{
    SeatEventType type;
    SeatDataSource* source;
    Seat*       seat;
};

union SeatEvent
{
    SeatEventType     type;
    SeatKeyboardEvent keyboard;
    SeatPointerEvent  pointer;
    SeatDataEvent     data;
};

using SeatEventHandlerFn = void(SeatEvent*);

void seat_client_set_event_handler(SeatClient*, std::move_only_function<SeatEventHandlerFn>&&);

enum class SeatEventFilterResult
{
    passthrough,
    capture,
};

struct SeatEventFilter;

auto seat_add_input_event_filter(Seat*, std::move_only_function<SeatEventFilterResult(SeatEvent*)>) -> Ref<SeatEventFilter>;
