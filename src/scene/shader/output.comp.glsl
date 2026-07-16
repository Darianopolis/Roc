#version 460
#extension GL_GOOGLE_include_directive : require

#include "common.glsl"

layout(push_constant, scalar) uniform PushConstants { SceneOutputInput pc; };

layout(local_size_x = SCENE_OUTPUT_DISPATCH_SIZE, local_size_y = SCENE_OUTPUT_DISPATCH_SIZE, local_size_z = 1) in;
void main()
{
    if (gl_GlobalInvocationID.x >= pc.extent.x || gl_GlobalInvocationID.y >= pc.extent.y) return;

    vec4f32 color = srgb_oetf(gpu_image_load(pc.source, vec2i32(gl_GlobalInvocationID.xy)));
    gpu_image_store(pc.target, vec2i32(gl_GlobalInvocationID.xy), color);
}
