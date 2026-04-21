#include "internal.hpp"

static
auto get_root(SceneNode* node) -> SceneTree*
{
    auto* root = dynamic_cast<SceneTree*>(node) ?: node->parent;
    if (!root) return nullptr;

    while (root->parent) {
        root = root->parent;
    }

    return root;
}

void scene_add_damage_listener(Scene* scene, SceneDamageListener listener)
{
    scene->damage_listeners.emplace_back(std::move(listener));
}

void scene_node_damage(SceneNode* node)
{
    auto* root = get_root(node);

    if (!root || !root->scene) {
        return;
    }

    node->damage(root->scene);
}

void scene_post_damage(Scene* scene, SceneNode* node)
{
    for (auto& listener : scene->damage_listeners) {
        listener(node);
    }
}
