#include "base.hpp"

#include "core/math.hpp"

SceneTree::~SceneTree()
{
    scene_node_unparent(this);

    for (auto& child : children) {
        child->parent = nullptr;
    }
}

void SceneTree::apply_damage(Scene* _scene)
{
    for (auto* child : children) {
        child->apply_damage(_scene);
    }
}

auto scene_tree_create() -> Ref<SceneTree>
{
    auto tree = ref_create<SceneTree>();
    tree->enabled = true;
    return tree;
}

void scene_tree_set_enabled(SceneTree* tree, bool enabled)
{
    if (tree->enabled == enabled) return;

    NODE_LOG("scene.tree{{{}}}.set_enabled({})", (void*)tree, enabled);

    if (enabled) {
        tree->enabled = true;
        scene_node_damage(tree);
    } else {
        scene_node_damage(tree);
        tree->enabled = false;
    }
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
            goto placed;
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

placed:
    NODE_LOG("scene.tree{{{}}}.place_{}({}, {})", (void*)tree, above ? "above" : "below", (void*)sibling, (void*)node);

    // TODO: We only need to damage regions that were visually affected by the rotate
    scene_node_damage(tree);
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
    scene_node_damage(tree);

    for (auto* child : tree->children) {
        child->parent = nullptr;
    }

    tree->children.clear();
}

void scene_tree_set_translation(SceneTree* tree, vec2f32 position)
{
    if (tree->translation == position) return;

    NODE_LOG("scene.tree{{{}}}.set_translation{}", (void*)tree, position);

    scene_node_damage(tree);
    tree->translation = position;
    scene_node_damage(tree);
}

auto scene_tree_get_position(SceneTree* tree) -> vec2f32
{
    return tree->translation + (tree->parent ? scene_tree_get_position(tree->parent) : vec2f32{});
}
