#include "base.hpp"

SceneNode::~SceneNode()
{
    debug_assert(!parent);
}

void scene_node_unparent(SceneNode* node)
{
    if (!node->parent) return;

    NODE_LOG("scene.node{{{}}}.unparent", (void*)node);

    scene_node_damage(node);
    auto parent = std::exchange(node->parent, nullptr);
    debug_assert(std::erase(parent->children, node) == 1);
}
