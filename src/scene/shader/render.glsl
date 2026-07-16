#include "render.h"

vec4f32 quad_sample(GPU_CONST_PTR(SceneQuad) quad, vec2f32 uv)
{
    // Sample
    vec4f32 sampled = srgb_eotf(gpu_image_sample(quad._.texture, uv));

    // Unpremultiply
    if ((quad._.flags & SCENE_DRAW_FLAG_PREMULTIPLIED) != 0 && sampled.a > 0) {
        sampled.rgb /= sampled.a;
    }

    // Tint
    vec4f32 tint = srgb_eotf(unpack_unorm4u8(quad._.tint));
    sampled *= tint;

    // Premultiply
    sampled.rgb *= sampled.a;

    return sampled;
}
