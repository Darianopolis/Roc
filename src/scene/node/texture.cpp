#include "base.hpp"

#include "core/math.hpp"

auto scene_texture_create() -> Ref<SceneTexture>
{
    auto texture = ref_create<SceneTexture>();
    texture->blend = GpuBlendMode::postmultiplied;
    texture->tint = {255, 255, 255, 255};
    texture->src = {{}, {1, 1}, minmax};
    return texture;
}

SceneTexture::~SceneTexture()
{
    scene_node_unparent(this);
}

void SceneTexture::apply_damage(Scene* scene)
{
    scene_post_damage(scene);
}

void scene_texture_set_image(SceneTexture* texture, GpuImage* image, GpuSampler* sampler, GpuBlendMode blend)
{
    bool damage = bool(texture->image.get())  != bool(image)
                    || texture->sampler.get() !=      sampler
                    || texture->blend         !=      blend;

    if (texture->image.get() == image && !damage) return;

#if SCENE_NOISY_NODES
    if (texture->image.get()   != image)   NODE_LOG("scene.texture{{{}}}.set_image({})",   (void*)texture, (void*)image);
    if (texture->sampler.get() != sampler) NODE_LOG("scene.texture{{{}}}.set_sampler({})", (void*)texture, (void*)sampler);
    if (texture->blend         != blend)   NODE_LOG("scene.texture{{{}}}.set_blend({})",   (void*)texture, blend);
#endif

    texture->image = image;
    texture->sampler = sampler;
    texture->blend = blend;

    if (damage) {
        scene_node_damage(texture);
    }
}

void scene_texture_set_tint(SceneTexture* texture, vec4u8 tint)
{
    if (texture->tint == tint) return;

    NODE_LOG("scene.texture{{{}}}.set_tint{}", (void*)texture, tint);

    texture->tint = tint;
    scene_node_damage(texture);
}

void scene_texture_set_src(SceneTexture* texture, aabb2f32 source)
{
    if (source == texture->src) return;

    NODE_LOG("scene.texture{{{}}}.set_src{}", (void*)texture, source);

    texture->src = source;
    scene_node_damage(texture);
}

void scene_texture_set_dst(SceneTexture* texture, rect2f32 extent)
{
    if (extent == texture->dst) return;

    NODE_LOG("scene.texture{{{}}}.set_dst{}", (void*)texture, extent);

    scene_node_damage(texture);
    texture->dst = extent;
    scene_node_damage(texture);
}

void scene_texture_damage(SceneTexture* texture, aabb2i32 damage)
{
    NODE_LOG("scene.texture{{{}}}.damage{}", (void*)texture, rect2i32(damage));

    scene_node_damage(texture);
}
