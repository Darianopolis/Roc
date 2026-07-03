#include "base.hpp"

SceneNode::~SceneNode()
{
    debug_assert(!parent);
}

void scene_node_unparent(SceneNode* node)
{
    if (!node->parent) return;

    NODE_LOG("scene.node{{{}}}.unparent", (void*)node);

    SceneDamage damage = {};
    scene_node_get_damage(node, {}, damage);
    scene_node_post_damage(node, {}, damage);

    auto parent = std::exchange(node->parent, nullptr);
    debug_assert(std::erase(parent->children, node) == 1);
}

void scene_node_post_damage(SceneNode* node, vec2f32 offset, SceneDamage damage)
{
    if (node->signals.damage) {
        node->signals.damage(offset, damage);
    }

    if (node->parent) {
        for (auto* child : node->parent->children | std::views::reverse) {
            if (child == node) break;
            scene_node_subtract_cover(child, child->translation - (offset + node->translation), damage);

            if (damage.region.empty() && !damage.types.contains(SceneDamageType::input)) {
                return;
            }
        }
        scene_node_post_damage(node->parent, offset + node->translation, damage);
    }
}
