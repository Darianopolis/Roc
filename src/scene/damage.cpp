#include "internal.hpp"

void scene_add_damage_listener(Scene* scene, SceneDamageListener* listener)
{
    log_debug("ADDED DAMAGE LISTENER");
    scene->damage_listeners.emplace_back(listener);
}

static
SceneTree* get_root(SceneNode* node)
{
    auto* root = dynamic_cast<SceneTree*>(node) ?: node->parent;
    if (!root) return nullptr;

    while (root->parent) {
        root = root->parent;
    }

    return root;
}

void scene_node_damage(SceneNode* node)
{
    auto* root = get_root(node);

    if (!root || !root->scene) {
        return;
    }

    node->apply_damage(root->scene);
}

void scene_post_damage(Scene* scene)
{
    for (auto* listener : scene->damage_listeners) {
        listener->damage(scene);
    }
}
