#include "internal.hpp"

#define SCENE_NOISY_NODES 0

#if SCENE_NOISY_NODES
#define NODE_LOG(...) log_error(__VA_ARGS__)
#else
#define NODE_LOG(...)
#endif

static
void request_frame(scene_context* ctx)
{
    for (auto* output : ctx->outputs) {
        scene_output_request_frame(output);
    }
}

static
void damage_node(scene_node* node)
{
    switch (node->type) {
        break;case scene_node_type::transform:
            for (auto* child : static_cast<scene_transform*>(node)->children) {
                damage_node(child);
            }
        break;case scene_node_type::tree:
            for (auto* child : static_cast<scene_tree*>(node)->children) {
                damage_node(child);
            }
        break;case scene_node_type::texture:
              case scene_node_type::mesh:
            if (node->parent) {
                request_frame(node->parent->ctx);
                // TODO: Damage affected regions only.
                //       Since currently we're just forcing a global redraw,
                //       we can immediately return after a frame has been requested.
                return;
            }
        break;case scene_node_type::input_region:
            ;
    }
}

// -----------------------------------------------------------------------------

scene_transform::~scene_transform()
{
    core_assert(children.empty());
}

auto scene_transform_create(scene_context*) -> ref<scene_transform>
{
    auto transform = core_create<scene_transform>();
    transform->type = scene_node_type::transform;
    transform->local.scale  = 1;
    transform->global.scale = 1;
    return transform;
}

static
void update_transform_global(scene_transform* transform, const scene_transform_state& parent)
{
    transform->global.translation = parent.translation + transform->local.translation * parent.scale;
    transform->global.scale       = parent.scale * transform->local.scale;
    for (auto* child : transform->children) {
        if (child->type == scene_node_type::transform) {
            update_transform_global(static_cast<scene_transform*>(child), transform->global);
        }
    }
}

void scene_transform_update(scene_transform* transform, vec2f32 translation, f32 scale)
{
    if (   transform->local.translation == translation
        && transform->local.scale       == scale) return;

#if SCENE_NOISY_NODES
    if (transform->local.translation != translation) NODE_LOG("scene.transform{{{}}}.set_translation{}", (void*)transform, core_to_string(translation));
    if (transform->local.scale       != scale)       NODE_LOG("scene.transform{{{}}}.set_scale({})",     (void*)transform, scale);
#endif

    damage_node(transform);
    transform->local.translation = translation;
    transform->local.scale = scale;
    auto* parent = transform->transform.get();
    update_transform_global(transform, parent ? parent->global : scene_transform_state{});
    damage_node(transform);
}

auto scene_transform_get_local(scene_transform* transform) -> scene_transform_state
{
    return transform->local;
}

auto scene_transform_get_global(scene_transform* transform) -> scene_transform_state
{
    return transform->global;
}

void scene_node_set_transform(scene_node* node, scene_transform* transform)
{
    if (node->transform.get() == transform) return;
    if (node->transform) {
        damage_node(node);
        core_assert(std::erase(node->transform->children, node) == 1);
    }
    node->transform = transform;
    if (transform) {
        transform->children.emplace_back(node);
        damage_node(node);
    }
}

scene_node::~scene_node()
{
    scene_node_set_transform(this, nullptr);
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
void tree_place(scene_tree* tree, scene_node* reference, scene_node* node, bool above)
{
    auto end = tree->children.end();
    auto cur =             std::ranges::find(tree->children, node);
    auto ref = reference ? std::ranges::find(tree->children, reference) : end;

    if (ref == end) {
        if (tree->children.empty()) {
            tree->children.push_back(node);
            return;
        }
        ref = above ? end - 1 : tree->children.begin();
    }

    if (cur == end) {
        if (above) tree->children.insert(ref + 1, node);
        else       tree->children.insert(ref,     node);
    } else if (cur != ref) {
        if (cur > ref) std::rotate(ref + i32(above), cur, cur + 1);
        else           std::rotate(cur, cur + 1, ref + i32(above));
    }

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
    if (   texture->image.get()   == image
        && texture->sampler.get() == sampler
        && texture->blend         == blend) return;

#if SCENE_NOISY_NODES
    if (texture->image.get()   != image)   NODE_LOG("scene.texture{{{}}}.set_image({})",   (void*)texture, (void*)image);
    if (texture->sampler.get() != sampler) NODE_LOG("scene.texture{{{}}}.set_sampler({})", (void*)texture, (void*)sampler);
    if (texture->blend         != blend)   NODE_LOG("scene.texture{{{}}}.set_blend({})",   (void*)texture, core_enum_to_string(blend));
#endif

    texture->image = image;
    texture->sampler = sampler;
    texture->blend = blend;
    damage_node(texture);
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

void scene_texture_damage(scene_texture* texture, aabb2f32 damage)
{
    core_assert_fail("scene_texture_damage", "TODO");
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
    if (mesh->blend         != blend)   NODE_LOG("scene.mesh{{{}}}.set_blend({})",   (void*)mesh, core_enum_to_string(blend));
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

    scene_update_pointer_focus(client->ctx);
}

auto scene_input_region_create(scene_client* client) -> ref<scene_input_region>
{
    auto region = core_create<scene_input_region>();
    region->type = scene_node_type::input_region;
    region->client = client;
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
}
