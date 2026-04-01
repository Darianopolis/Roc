#include "internal.hpp"

#include <core/math.hpp>

#define SCENE_NOISY_NODES 0

#if SCENE_NOISY_NODES
#define NODE_LOG(...) log_error(__VA_ARGS__)
#else
#define NODE_LOG(...)
#endif

static
auto get_enabled_tree_root(SceneTree* tree) -> SceneTree*
{
    if (!tree->enabled) return nullptr;

    return tree->parent ? get_enabled_tree_root(tree->parent) : tree;
}

static
auto is_enabled_in_scene(SceneNode* node)
{
    auto* tree = node->type == SceneNodeType::tree
        ? static_cast<SceneTree*>(node)
        : node->parent;

    return tree && get_enabled_tree_root(tree) == tree->scene->root_tree.get();
}

// -----------------------------------------------------------------------------

static
void dispatch_damage(Scene* scene)
{
    defer { scene->damage.queued = {}; };

    if (scene->damage.queued.contains(SceneDamageType::input)) {
        scene_update_pointers(scene);
    }

    if (scene->damage.queued.contains(SceneDamageType::visual)) {
        for (auto* output : scene->outputs) {
            scene_output_request_frame(output);
        }
    }
}

static
void enqueue_damage(Scene* scene, SceneDamageType type)
{
    auto enqueue = scene->damage.queued.empty();
    scene->damage.queued |= type;

    if (!enqueue) return;

    exec_enqueue(scene->exec, [scene = Weak(scene)] {
        if (scene) dispatch_damage(scene.get());
    });
}

// -----------------------------------------------------------------------------

static
void basic_damage(SceneNode* node)
{
    if (node->parent) {
        // TODO: Damage affected regions only.
        enqueue_damage(node->parent->scene, SceneDamageType::visual);
    }
}

// -----------------------------------------------------------------------------

template<bool CheckInScene = true>
void damage_node(SceneTexture* texture)
{
    if (CheckInScene && !is_enabled_in_scene(texture)) return;

    return basic_damage(texture);
}

template<bool CheckInScene = true>
void damage_node(SceneMesh* mesh)
{
    if (CheckInScene && !is_enabled_in_scene(mesh)) return;

    return basic_damage(mesh);
}

template<bool CheckInScene = true>
void damage_node(SceneInputRegion* input)
{
    if (CheckInScene && !is_enabled_in_scene(input)) return;

    auto* scene = input->client->scene;
    enqueue_damage(scene, SceneDamageType::input);
}

static
void damage_node(SceneTree* tree)
{
    if (!is_enabled_in_scene(tree)) return;

    scene_iterate<SceneIterateDirection::back_to_front>(tree,
        scene_iterate_default,
        [](auto* node) { damage_node<false>(node); },
        scene_iterate_default);
}

static
void damage_node(SceneNode* node)
{
    if (!is_enabled_in_scene(node)) return;

    scene_visit(node, [](auto* node) { damage_node(node); });
}

// -----------------------------------------------------------------------------

SceneNode::~SceneNode()
{
    debug_assert(!parent);
}

// -----------------------------------------------------------------------------

SceneTree::~SceneTree()
{
    for (auto& child : children) {
        child->parent = nullptr;
    }
}

auto scene_tree_create(Scene* scene) -> Ref<SceneTree>
{
    auto tree = ref_create<SceneTree>();
    tree->type = SceneNodeType::tree;
    tree->scene = scene;
    tree->enabled = true;
    return tree;
}

void scene_tree_set_enabled(SceneTree* tree, bool enabled)
{
    if (tree->enabled == enabled) return;

    NODE_LOG("scene.tree{{{}}}.set_enabled({})", (void*)tree, enabled);

    if (enabled) {
        tree->enabled = true;
        damage_node(tree);
    } else {
        damage_node(tree);
        tree->enabled = false;
    }
}

void scene_node_unparent(SceneNode* node)
{
    if (!node->parent) return;

    NODE_LOG("scene.node{{{}}}.unparent", (void*)node);

    damage_node(node);
    auto parent = std::exchange(node->parent, nullptr);
    debug_assert(parent->children.erase(node) == 1);
}

static
void reparent_unsafe(SceneNode* node, SceneTree* tree)
{
    if (node->parent == tree) return;
    if (node->parent) {
        scene_node_unparent(node);
    }
    node->parent = tree;
}

static
void tree_place(SceneTree* tree, SceneNode* sibling, SceneNode* node, bool above)
{
    auto end = tree->children.end();
    auto cur =           std::ranges::find(tree->children, node);
    auto sib = sibling ? std::ranges::find(tree->children, sibling) : end;

    if (sib == end) {
        if (tree->children.empty()) {
            tree->children.emplace_back(node);
            return;
        }
        sib = above ? end - 1 : tree->children.begin();
    }

    if (cur == end) {
        if (above) tree->children.insert(sib + 1, node);
        else       tree->children.insert(sib,     node);
    } else if (cur != sib) {
        if (cur > sib) std::rotate(sib + i32(above), cur, cur + 1);
        else           std::rotate(cur, cur + 1, sib + i32(above));
    }

    NODE_LOG("scene.tree{{{}}}.place_{}({}, {})", (void*)tree, above ? "above" : "below", (void*)sibling, (void*)node);

    // TODO: We only need to damage regions that were visually affected by the rotate
    damage_node(tree);
}

void scene_tree_place_below(SceneTree* tree, SceneNode* reference, SceneNode* to_place)
{
    tree_place(tree, reference, to_place, false);
    reparent_unsafe(to_place, tree);

}

void scene_tree_place_above(SceneTree* tree, SceneNode* reference, SceneNode* to_place)
{
    tree_place(tree, reference, to_place, true);
    reparent_unsafe(to_place, tree);
}

void scene_tree_clear(SceneTree* tree)
{
    damage_node(tree);

    for (auto* child : tree->children) {
        child->parent = nullptr;
    }

    tree->children.clear();
}

void scene_tree_set_translation(SceneTree* tree, vec2f32 position)
{
    if (tree->translation == position) return;

    NODE_LOG("scene.tree{{{}}}.set_translation{}", (void*)tree, position);

    damage_node(tree);
    tree->translation = position;
    damage_node(tree);
}

// -----------------------------------------------------------------------------

auto scene_texture_create(Scene*) -> Ref<SceneTexture>
{
    auto texture = ref_create<SceneTexture>();
    texture->blend = GpuBlendMode::postmultiplied;
    texture->type = SceneNodeType::texture;
    texture->tint = {255, 255, 255, 255};
    texture->src = {{}, {1, 1}, minmax};
    return texture;
}

void scene_texture_set_image(SceneTexture* texture, GpuImage* image, GpuSampler* sampler, GpuBlendMode blend)
{
    bool damage = bool(texture->image.get())  != bool(texture)
                    || texture->sampler.get() !=      sampler
                    || texture->blend         !=      blend;

    if (texture->image.get() == image && !damage) return;

#if SCENE_NOISY_NODES
    if (texture->image.get()   != image)   NODE_LOG("scene.texture{{{}}}.set_image({})",   (void*)texture, (void*)image);
    if (texture->sampler.get() != sampler) NODE_LOG("scene.texture{{{}}}.set_sampler({})", (void*)texture, (void*)sampler);
    if (texture->blend         != blend)   NODE_LOG("scene.texture{{{}}}.set_blend({})",   (void*)texture, blend);
#endif

    texture->image = image;
    texture->sampler = sampler;
    texture->blend = blend;

    if (damage) {
        damage_node(texture);
    }
}

void scene_texture_set_tint(SceneTexture* texture, vec4u8 tint)
{
    if (texture->tint == tint) return;

    NODE_LOG("scene.texture{{{}}}.set_tint{}", (void*)texture, tint);

    texture->tint = tint;
    damage_node(texture);
}

void scene_texture_set_src(SceneTexture* texture, aabb2f32 source)
{
    if (source == texture->src) return;

    NODE_LOG("scene.texture{{{}}}.set_src{}", (void*)texture, source);

    texture->src = source;
    damage_node(texture);
}

void scene_texture_set_dst(SceneTexture* texture, rect2f32 extent)
{
    if (extent == texture->dst) return;

    NODE_LOG("scene.texture{{{}}}.set_dst{}", (void*)texture, extent);

    damage_node(texture);
    texture->dst = extent;
    damage_node(texture);
}

void scene_texture_damage(SceneTexture* texture, aabb2i32 damage)
{
    NODE_LOG("scene.texture{{{}}}.damage{}", (void*)texture, rect2i32(damage));

    damage_node(texture);
}

// -----------------------------------------------------------------------------

auto scene_mesh_create(Scene* scene) -> Ref<SceneMesh>
{
    auto mesh = ref_create<SceneMesh>();
    mesh->type = SceneNodeType::mesh;
    return mesh;
}

void scene_mesh_update(SceneMesh* mesh, std::span<const SceneVertex>      vertices,
                                        std::span<const u16>              indices,
                                        std::span<const SceneMeshSegment> segments,
                                        vec2f32 offset)
{
    static constexpr auto changed = [](auto& a, auto& b) {
        return a.size() != b.size() || memcmp(a.data(), b.data(), b.size_bytes()) != 0;
    };

    bool segments_dirty = changed(mesh->segments, segments);
    bool vertices_dirty = changed(mesh->vertices, vertices);
    bool indices_dirty  = changed(mesh->indices,  indices);

    if (!segments_dirty && !vertices_dirty && !indices_dirty && mesh->offset == offset) return;

    damage_node(mesh);

    mesh->offset = offset;

    static constexpr auto update = [](auto& a, auto& b) {
        a.resize(b.size());
        memcpy(a.data(), b.data(), b.size_bytes());
    };

    if (segments_dirty) {
        mesh->segments.resize(segments.size());
        std::ranges::copy(segments, mesh->segments.data());
    }
    if (vertices_dirty) update(mesh->vertices, vertices);
    if (indices_dirty)  update(mesh->indices,  indices);

#if SCENE_NOISY_NODES
    NODE_LOG("scene.mesh{{{}}}.damage()", (void*)mesh);
#endif

    damage_node(mesh);
}

// -----------------------------------------------------------------------------

SceneInputRegion::~SceneInputRegion()
{
    client->input_regions--;

    if (parent) {
        scene_node_unparent(this);
    }

    for (auto* seat : scene_get_seats(client->scene)) {
        if (seat->keyboard->focus == this) {
            scene_keyboard_focus(seat->keyboard.get(), nullptr);
        }
    }

    scene_update_pointers(client->scene);
}

auto scene_input_region_create(SceneClient* client, SceneWindow* window) -> Ref<SceneInputRegion>
{
    auto region = ref_create<SceneInputRegion>();
    region->type = SceneNodeType::input_region;
    region->client = client;
    region->window = window;

    client->input_regions++;

    return region;
}

void scene_input_region_set_region(SceneInputRegion* input_region, region2f32 region)
{
    if (input_region->region == region) return;

#if SCENE_NOISY_NODES
    NODE_LOG("scene.input_region{{{}}}.set_region([{:s}])", (void*)input_region,
        region.aabbs
            | std::views::transform([&](auto& aabb) { return std::format("{}", aabb); })
            | std::views::join_with(", "sv));
#endif

    input_region->region = std::move(region);

    damage_node(input_region);
}

void scene_input_region_set_window(SceneInputRegion* input_region, SceneWindow* window)
{
    input_region->window = window;
}
