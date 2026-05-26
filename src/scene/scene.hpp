#pragma once

#include <core/region.hpp>
#include <core/object.hpp>
#include <core/id.hpp>
#include <gpu/gpu.hpp>

#include "shader/render.h"

// -----------------------------------------------------------------------------

struct SceneNode;
struct SceneTree;
struct SceneTexture;

struct Scene;

auto scene_create(Gpu*) -> Ref<Scene>;

auto scene_get_root(Scene*) -> SceneTree*;

// -----------------------------------------------------------------------------

void scene_render(Scene*, GpuImage* target, rect2f32 viewport);

// -----------------------------------------------------------------------------

using SceneDamageListener = std::move_only_function<void(SceneNode*)>;
void scene_add_damage_listener(Scene*, SceneDamageListener);

// -----------------------------------------------------------------------------

struct SceneNode
{
    SceneTree* parent;

    virtual ~SceneNode();

    virtual void damage(Scene*) = 0;
};

void scene_node_unparent(SceneNode*);
void scene_node_damage(  SceneNode*);

void scene_post_damage(Scene*, SceneNode*);

// -----------------------------------------------------------------------------

struct SceneTree : SceneNode
{
    Scene* scene;

    bool enabled;
    f32 opacity = 1.f;

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
void scene_tree_replace(    SceneTree*, std::span<SceneNode* const> children);
void scene_tree_clear(      SceneTree*);

void scene_tree_set_opacity(SceneTree*, f32 opacity);

void scene_tree_set_translation(SceneTree*, vec2f32 translation);
auto scene_tree_get_position(   SceneTree*) -> vec2f32;

// -----------------------------------------------------------------------------

struct SceneInputRegion : SceneNode
{
    region2f32 region = {{{-INFINITY, -INFINITY}, {INFINITY, INFINITY}, minmax}};
    rect2f32   clip;

    virtual void damage(Scene*);

    ~SceneInputRegion();
};

auto scene_input_region_create() -> Ref<SceneInputRegion>;
void scene_input_region_set_region(SceneInputRegion*, region2f32);
void scene_input_region_set_clip(SceneInputRegion*, rect2f32);
auto scene_find_input_region_at(SceneTree*, vec2f32 pos) -> SceneInputRegion*;

// -----------------------------------------------------------------------------

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
