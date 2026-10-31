#type COMPUTE
#version 450
#extension GL_GOOGLE_include_directive : require

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

#include "../common/motion_vectors.glsl"

layout(set = 0, binding = 0) uniform sampler2D depthBuffer;

// VK-1662: camera-relative clip-to-clip matrices built on the CPU in double (buildMotionVectorMatrices), so no fp32
// world position is ever formed. Mirrors render::upscaling::MotionVectorParams (std140 offsets 0 / 64 / 128).
layout(std140, set = 0, binding = 1) uniform MotionVectorParams {
    mat4 jitteredToCurrentClip;   // P_unjittered * inverse(P_jittered)
    mat4 jitteredToPreviousClip;  // P_prev,unjittered * V_prev * inverse(V) * inverse(P_jittered)
    vec4 screenParams;            // xy = resolution, zw = 1/resolution
};

layout(set = 0, binding = 2, rg16f) uniform writeonly image2D motionVectorImage;

void main()
{
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 resolution = ivec2(screenParams.xy);

    if (pixel.x >= resolution.x || pixel.y >= resolution.y)
        return;

    // The depth buffer was rasterised with the JITTERED projection, so the pixel centre plus its depth is a point
    // in jittered clip space (w = 1). Sky pixels (depth 1 = the far plane) reproject too: their motion is exact
    // under camera rotation and ~0 under translation, as for a distant sky.
    vec2 uv = (vec2(pixel) + 0.5) * screenParams.zw;
    vec4 jitteredClip = vec4(uv * 2.0 - 1.0, texelFetch(depthBuffer, pixel, 0).r, 1.0);

    vec2 mv = motionVectorNdc(jitteredToCurrentClip * jitteredClip, jitteredToPreviousClip * jitteredClip);

    imageStore(motionVectorImage, pixel, vec4(mv, 0.0, 0.0));
}
