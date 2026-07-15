#include "base.hpp"

#include <core/math.hpp>

auto scene_texture_create() -> Ref<SceneTexture>
{
    auto texture = ref_create<SceneTexture>();
    texture->type = SceneNodeType::texture;
    texture->flags = {};
    texture->tint = {1, 1, 1, 1};
    texture->src = {{}, {1, 1}, minmax};
    return texture;
}

SceneTexture::~SceneTexture()
{
    scene_node_unparent(this);
}

void scene_node_get_damage(SceneTexture* texture, vec2f32 offset, SceneDamage& damage)
{
    rect2f32 dst = texture->dst;
    dst.origin += offset;
    region_op<RegionOpUnion>(damage.region, damage.region, RegionSingle<f32>(dst));
    damage.types |= SceneDamageType::visual;
}

void scene_node_subtract_cover(SceneTexture* texture, vec2f32 offset, SceneDamage& damage)
{
    if (texture->image || texture->tint.w != 255) return;

    rect2f32 dst = texture->dst;
    dst.origin += offset;
    region_op<RegionOpSubtract>(damage.region, damage.region, RegionSingle<f32>(dst));
}

static
void damage(SceneTexture* texture, rect2f32 dst)
{
    if (!scene_node_has_any_damage_listeners(texture)) return;
    scene_node_post_damage(texture, {}, {{dst}, SceneDamageType::visual});
}

void scene_texture_set_image(SceneTexture* texture, GpuImage* image, GpuSampler* sampler, Flags<SceneTextureFlag> flags)
{
    bool changed = bool(texture->image.get())  != bool(image)
                     || texture->sampler.get() !=      sampler
                     || texture->flags         !=      flags;

    if (texture->image.get() == image && !changed) return;

#if SCENE_NOISY_NODES
    if (texture->image.get()   != image)   NODE_LOG("scene.texture{{{}}}.set_image({})",   (void*)texture, (void*)image);
    if (texture->sampler.get() != sampler) NODE_LOG("scene.texture{{{}}}.set_sampler({})", (void*)texture, (void*)sampler);
    if (texture->flags         != flags)   NODE_LOG("scene.texture{{{}}}.set_flags({})",   (void*)texture, flags);
#endif

    texture->image = image;
    texture->sampler = sampler;
    texture->flags = flags;

    if (changed) {
        damage(texture, texture->dst);
    }
}

void scene_texture_set_tint(SceneTexture* texture, vec4f32 tint)
{
    if (texture->tint == tint) return;

    NODE_LOG("scene.texture{{{}}}.set_tint{}", (void*)texture, tint);

    texture->tint = tint;
    damage(texture, texture->dst);
}

void scene_texture_set_src(SceneTexture* texture, aabb2f32 source)
{
    if (source == texture->src) return;

    NODE_LOG("scene.texture{{{}}}.set_src{}", (void*)texture, source);

    texture->src = source;
    damage(texture, texture->dst);
}

void scene_texture_set_dst(SceneTexture* texture, rect2f32 dst)
{
    if (dst == texture->dst) return;

    NODE_LOG("scene.texture{{{}}}.set_dst{}", (void*)texture, dst);

    damage(texture, texture->dst);
    texture->dst = dst;
    damage(texture, texture->dst);
}

void scene_texture_damage(SceneTexture* texture, aabb2i32 region)
{
    // Clamp to image
    auto pixel_extent = vec_cast<i32>(texture->image->base()->extent);
    region = aabb_inner(region, {{}, pixel_extent, minmax});

    // Transfrom from pixels -> scene
    aabb2f32 transformed = rect_cast<f32>(region);
    vec2f32 pixels_to_layout = texture->dst.extent / vec_cast<f32>(pixel_extent);
    transformed.min = transformed.min * pixels_to_layout + texture->dst.origin;
    transformed.max = transformed.max * pixels_to_layout + texture->dst.origin;

    damage(texture, transformed);
}
