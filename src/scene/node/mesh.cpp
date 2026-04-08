#include "base.hpp"

auto scene_mesh_create() -> Ref<SceneMesh>
{
    auto mesh = ref_create<SceneMesh>();
    return mesh;
}

SceneMesh::~SceneMesh()
{
    scene_node_unparent(this);
}

void SceneMesh::apply_damage(Scene* scene)
{
    scene_post_damage(scene);
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

    scene_node_damage(mesh);

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

    scene_node_damage(mesh);
}
