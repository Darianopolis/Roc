#include "internal.hpp"

Scene::~Scene()
{
}

auto scene_create(ExecContext* exec, Gpu* gpu) -> Ref<Scene>
{
    auto scene = ref_create<Scene>();

    scene->exec = exec;
    scene->gpu = gpu;

    scene->root = scene_tree_create();
    scene->root->scene = scene.get();

    scene_render_init(scene.get());

    return scene;
}

auto scene_get_root(Scene* scene) -> SceneTree*
{
    return scene->root.get();
}
