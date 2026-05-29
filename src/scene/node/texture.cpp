#include "base.hpp"

#include <core/math.hpp>

auto scene_texture_create() -> Ref<SceneTexture>
{
    auto texture = ref_create<SceneTexture>();
    texture->type = SceneNodeType::texture;
    texture->blend = GpuBlendMode::postmultiplied;
    texture->tint = {255, 255, 255, 255};
    texture->src = {{}, {1, 1}, minmax};
    return texture;
}

SceneTexture::~SceneTexture()
{
    scene_node_unparent(this);
}

auto scene_node_get_damage(SceneTexture* texture) -> SceneDamage
{
    return {texture->dst, SceneDamageType::visual};
}

static
void damage(SceneTexture* texture)
{
    scene_node_post_damage(texture, scene_node_get_damage(texture));
}

void scene_texture_set_image(SceneTexture* texture, GpuImage* image, GpuSampler* sampler, GpuBlendMode blend)
{
    bool changed = bool(texture->image.get())  != bool(image)
                     || texture->sampler.get() !=      sampler
                     || texture->blend         !=      blend;

    if (texture->image.get() == image && !changed) return;

#if SCENE_NOISY_NODES
    if (texture->image.get()   != image)   NODE_LOG("scene.texture{{{}}}.set_image({})",   (void*)texture, (void*)image);
    if (texture->sampler.get() != sampler) NODE_LOG("scene.texture{{{}}}.set_sampler({})", (void*)texture, (void*)sampler);
    if (texture->blend         != blend)   NODE_LOG("scene.texture{{{}}}.set_blend({})",   (void*)texture, blend);
#endif

    texture->image = image;
    texture->sampler = sampler;
    texture->blend = blend;

    if (changed) {
        damage(texture);
    }
}

void scene_texture_set_tint(SceneTexture* texture, vec4u8 tint)
{
    if (texture->tint == tint) return;

    NODE_LOG("scene.texture{{{}}}.set_tint{}", (void*)texture, tint);

    texture->tint = tint;
    damage(texture);
}

void scene_texture_set_src(SceneTexture* texture, aabb2f32 source)
{
    if (source == texture->src) return;

    NODE_LOG("scene.texture{{{}}}.set_src{}", (void*)texture, source);

    texture->src = source;
    damage(texture);
}

void scene_texture_set_dst(SceneTexture* texture, rect2f32 dst)
{
    if (dst == texture->dst) return;

    NODE_LOG("scene.texture{{{}}}.set_dst{}", (void*)texture, dst);

    damage(texture);
    texture->dst = dst;
    damage(texture);
}

void scene_texture_damage(SceneTexture* texture, aabb2i32 region)
{
    // Clamp to image
    auto pixel_extent = vec_cast<i32>(texture->image->extent());
    region = aabb_inner(region, {{}, pixel_extent, minmax});

    // Transfrom from pixels -> scene
    aabb2f32 transformed = rect_cast<f32>(region);
    vec2f32 pixels_to_layout = texture->dst.extent / vec_cast<f32>(pixel_extent);
    transformed.min = transformed.min * pixels_to_layout + texture->dst.origin;
    transformed.max = transformed.max * pixels_to_layout + texture->dst.origin;

    scene_node_post_damage(texture, {transformed, SceneDamageType::visual});
}
