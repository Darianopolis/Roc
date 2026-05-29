#include "base.hpp"

SceneNode::~SceneNode()
{
    debug_assert(!parent);
}

void scene_node_unparent(SceneNode* node)
{
    if (!node->parent) return;

    NODE_LOG("scene.node{{{}}}.unparent", (void*)node);

    scene_node_post_damage(node, scene_node_get_damage(node));
    auto parent = std::exchange(node->parent, nullptr);
    debug_assert(std::erase(parent->children, node) == 1);
}

void scene_node_post_damage(SceneNode* node, SceneDamage damage)
{
    if (node->signals.damage) {
        node->signals.damage(damage);
    }

    if (node->parent) {
        damage.region.min += node->translation;
        damage.region.max += node->translation;
        scene_node_post_damage(node->parent, damage);
    }
}
