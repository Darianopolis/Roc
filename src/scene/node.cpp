#include "internal.hpp"

#define SCENE_NOISY_NODES 0

#if SCENE_NOISY_NODES
#define NODE_LOG(...) log_error(__VA_ARGS__)
#else
#define NODE_LOG(...)
#endif

static
auto get_enabled_tree_root(scene_tree* tree) -> scene_tree*
{
    if (!tree->enabled) return nullptr;

    return tree->parent ? get_enabled_tree_root(tree->parent) : tree;
}

static
auto is_enabled_in_scene(scene_node* node)
{
    auto* tree = node->type == scene_node_type::tree
        ? static_cast<scene_tree*>(node)
        : node->parent;

    return tree && get_enabled_tree_root(tree) == tree->ctx->root_tree.get();
}

// -----------------------------------------------------------------------------

static
void dispatch_damage(scene_context* ctx)
{
    defer { ctx->damage.queued = {}; };

    if (ctx->damage.queued.contains(scene_damage_type::input)) {
        scene_update_pointers(ctx);
    }

    if (ctx->damage.queued.contains(scene_damage_type::visual)) {
        for (auto* output : ctx->outputs) {
            scene_output_request_frame(output);
        }
    }
}

static
void enqueue_damage(scene_context* ctx, scene_damage_type type)
{
    auto enqueue = ctx->damage.queued.empty();
    ctx->damage.queued |= type;

    if (!enqueue) return;

    exec_enqueue(ctx->exec, [ctx = weak(ctx)] {
        if (ctx) dispatch_damage(ctx.get());
    });
}

// -----------------------------------------------------------------------------

static
void basic_damage(scene_node* node)
{
    if (node->parent) {
        // TODO: Damage affected regions only.
        enqueue_damage(node->parent->ctx, scene_damage_type::visual);
    }
}

// -----------------------------------------------------------------------------

template<bool CheckInScene = true>
void damage_node(scene_texture* texture)
{
    if (CheckInScene && !is_enabled_in_scene(texture)) return;

    return basic_damage(texture);
}

template<bool CheckInScene = true>
void damage_node(scene_mesh* mesh)
{
    if (CheckInScene && !is_enabled_in_scene(mesh)) return;

    return basic_damage(mesh);
}

template<bool CheckInScene = true>
void damage_node(scene_input_region* input)
{
    if (CheckInScene && !is_enabled_in_scene(input)) return;

    auto* ctx = input->client->ctx;
    enqueue_damage(ctx, scene_damage_type::input);
}

static
void damage_node(scene_tree* tree)
{
    if (!is_enabled_in_scene(tree)) return;

    scene_iterate<scene_iterate_direction::back_to_front>(tree,
        scene_iterate_default,
        [](auto* node) { damage_node<false>(node); },
        scene_iterate_default);
}

static
void damage_node(scene_node* node)
{
    if (!is_enabled_in_scene(node)) return;

    scene_visit(node, [](auto* node) { damage_node(node); });
}

// -----------------------------------------------------------------------------

scene_node::~scene_node()
{
    core_assert(!parent);
}

// -----------------------------------------------------------------------------

scene_tree::~scene_tree()
{
    for (auto& child : children) {
        child->parent = nullptr;
    }
}

auto scene_tree_create(scene_context* ctx) -> ref<scene_tree>
{
    auto tree = core_create<scene_tree>();
    tree->type = scene_node_type::tree;
    tree->ctx = ctx;
    tree->enabled = true;
    return tree;
}

void scene_tree_set_enabled(scene_tree* tree, bool enabled)
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

void scene_node_unparent(scene_node* node)
{
    if (!node->parent) return;

    NODE_LOG("scene.node{{{}}}.unparent", (void*)node);

    damage_node(node);
    auto parent = std::exchange(node->parent, nullptr);
    core_assert(parent->children.erase(node) == 1);
}

static
void reparent_unsafe(scene_node* node, scene_tree* tree)
{
    if (node->parent == tree) return;
    if (node->parent) {
        scene_node_unparent(node);
    }
    node->parent = tree;
}

static
void tree_place(scene_tree* tree, scene_node* sibling, scene_node* node, bool above)
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

    NODE_LOG("scene.tree{{{}}}.place_{}({}, {})", (void*)tree, above ? "above" : "below", (void*)reference, (void*)node);

    // TODO: We only need to damage regions that were visually affected by the rotate
    damage_node(tree);
}

void scene_tree_place_below(scene_tree* tree, scene_node* reference, scene_node* to_place)
{
    tree_place(tree, reference, to_place, false);
    reparent_unsafe(to_place, tree);

}

void scene_tree_place_above(scene_tree* tree, scene_node* reference, scene_node* to_place)
{
    tree_place(tree, reference, to_place, true);
    reparent_unsafe(to_place, tree);
}

void scene_tree_clear(scene_tree* tree)
{
    damage_node(tree);

    for (auto* child : tree->children) {
        child->parent = nullptr;
    }

    tree->children.clear();
}

void scene_tree_set_translation(scene_tree* tree, vec2f32 position)
{
    if (tree->translation == position) return;

    NODE_LOG("scene.tree{{{}}}.set_translation{}", (void*)tree, core_to_string(position));

    damage_node(tree);
    tree->translation = position;
    damage_node(tree);
}

// -----------------------------------------------------------------------------

auto scene_texture_create(scene_context*) -> ref<scene_texture>
{
    auto texture = core_create<scene_texture>();
    texture->blend = gpu_blend_mode::postmultiplied;
    texture->type = scene_node_type::texture;
    texture->tint = {255, 255, 255, 255};
    texture->src = {{}, {1, 1}, core_minmax};
    return texture;
}

void scene_texture_set_image(scene_texture* texture, gpu_image* image, gpu_sampler* sampler, gpu_blend_mode blend)
{
    bool damage = bool(texture->image.get())  != bool(texture)
                    || texture->sampler.get() !=      sampler
                    || texture->blend         !=      blend;

    if (texture->image.get() == image && !damage) return;

#if SCENE_NOISY_NODES
    if (texture->image.get()   != image)   NODE_LOG("scene.texture{{{}}}.set_image({})",   (void*)texture, (void*)image);
    if (texture->sampler.get() != sampler) NODE_LOG("scene.texture{{{}}}.set_sampler({})", (void*)texture, (void*)sampler);
    if (texture->blend         != blend)   NODE_LOG("scene.texture{{{}}}.set_blend({})",   (void*)texture, core_to_string(blend));
#endif

    texture->image = image;
    texture->sampler = sampler;
    texture->blend = blend;

    if (damage) {
        damage_node(texture);
    }
}

void scene_texture_set_tint(scene_texture* texture, vec4u8 tint)
{
    if (texture->tint == tint) return;

    NODE_LOG("scene.texture{{{}}}.set_tint{}", (void*)texture, core_to_string(tint));

    texture->tint = tint;
    damage_node(texture);
}

void scene_texture_set_src(scene_texture* texture, aabb2f32 source)
{
    if (source == texture->src) return;

    NODE_LOG("scene.texture{{{}}}.set_src{}", (void*)texture, core_to_string(source));

    texture->src = source;
    damage_node(texture);
}

void scene_texture_set_dst(scene_texture* texture, rect2f32 extent)
{
    if (extent == texture->dst) return;

    NODE_LOG("scene.texture{{{}}}.set_dst{}", (void*)texture, core_to_string(extent));

    damage_node(texture);
    texture->dst = extent;
    damage_node(texture);
}

void scene_texture_damage(scene_texture* texture, aabb2i32 damage)
{
    NODE_LOG("scene.texture{{{}}}.damage{}", (void*)texture, core_to_string(rect2i32(damage)));

    damage_node(texture);
}

// -----------------------------------------------------------------------------

auto scene_mesh_create(scene_context* ctx) -> ref<scene_mesh>
{
    auto mesh = core_create<scene_mesh>();
    mesh->type = scene_node_type::mesh;
    return mesh;
}

void scene_mesh_update(scene_mesh* mesh, gpu_image* image, gpu_sampler* sampler, gpu_blend_mode blend, aabb2f32 clip, std::span<const scene_vertex> vertices, std::span<const u16> indices)
{
#if SCENE_NOISY_NODES
    if (mesh->image.get()   != image)   NODE_LOG("scene.mesh{{{}}}.set_image({})",   (void*)mesh, (void*)image);
    if (mesh->sampler.get() != sampler) NODE_LOG("scene.mesh{{{}}}.set_sampler({})", (void*)mesh, (void*)sampler);
    if (mesh->blend         != blend)   NODE_LOG("scene.mesh{{{}}}.set_blend({})",   (void*)mesh, core_to_string(blend));
    if (mesh->clip          != clip)    NODE_LOG("scene.mesh{{{}}}.set_clip{}",      (void*)mesh, core_to_string(clip));

    NODE_LOG("scene.mesh{{{}}}.set_vertices({}, {})", (void*)mesh, (void*)vertices.data(), vertices.size());
    NODE_LOG("scene.mesh{{{}}}.set_indices({}, {})",  (void*)mesh, (void*)indices.data(),  indices.size());
#endif

    damage_node(mesh);
    mesh->image = image;
    mesh->sampler = sampler;
    mesh->blend = blend;
    mesh->clip = clip;
    mesh->vertices.assign_range(vertices);
    mesh->indices.assign_range(indices);
    damage_node(mesh);
}

// -----------------------------------------------------------------------------

scene_input_region::~scene_input_region()
{
    client->input_regions--;

    if (parent) {
        scene_node_unparent(this);
    }

    for (auto* seat : scene_get_seats(client->ctx)) {
        if (seat->keyboard->focus.region == this) {
            scene_keyboard_set_focus(seat->keyboard.get(), {});
        }
    }

    scene_update_pointers(client->ctx);
}

auto scene_input_region_create(scene_client* client, scene_window* window) -> ref<scene_input_region>
{
    auto region = core_create<scene_input_region>();
    region->type = scene_node_type::input_region;
    region->client = client;
    region->window = window;
    client->input_regions++;
    return region;
}

void scene_input_region_set_region(scene_input_region* input_region, region2f32 region)
{
    if (input_region->region == region) return;

#if SCENE_NOISY_NODES
    NODE_LOG("scene.input_region{{{}}}.set_region([{:s}])", (void*)input_region,
        region.aabbs
            | std::views::transform((std::string(&)(const aabb2f32&))core_to_string)
            | std::views::join_with(", "sv));
#endif

    input_region->region = std::move(region);

    damage_node(input_region);
}

void scene_input_region_set_window(scene_input_region* input_region, scene_window* window)
{
    input_region->window = window;
}
