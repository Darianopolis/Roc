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

struct Scene;

auto scene_create(ExecContext*, Gpu*) -> Ref<Scene>;

auto scene_get_root(Scene*) -> SceneTree*;

void scene_render(Scene*, GpuImage* target, rect2f32 viewport);

// -----------------------------------------------------------------------------

struct SceneDamageListener
{
    virtual void damage(Scene*) = 0;
};

void scene_add_damage_listener(Scene*, SceneDamageListener*);

// -----------------------------------------------------------------------------

struct SceneNode
{
    SceneTree* parent;

    virtual ~SceneNode();

    virtual void apply_damage(Scene*) = 0;
    virtual void visit_children(void(*visit)(void*, SceneNode*), void* userdata) {}
};

void scene_node_unparent(SceneNode*);
void scene_node_damage(  SceneNode*);

struct SceneTree : SceneNode
{
    Scene* scene;

    vec2f32 translation;
    bool enabled;

    std::vector<SceneNode*> children;

    virtual void apply_damage(Scene*);

    virtual void visit_children(void(*visit)(void*, SceneNode*), void* userdata)
    {
        for (auto* child : children) {
            visit(userdata, child);
        }
    }

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

    virtual void apply_damage(Scene*);

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

    virtual void apply_damage(Scene*);

    ~SceneMesh();
};

auto scene_mesh_create() -> Ref<SceneMesh>;
void scene_mesh_update(SceneMesh*, std::span<const SceneVertex>      vertices,
                                   std::span<const u16>              indices,
                                   std::span<const SceneMeshSegment> segments,
                                   vec2f32 offset);
