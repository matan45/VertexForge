#type COMPUTE
#version 450
#extension GL_GOOGLE_include_directive : require

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

#include "../common/motion_vectors.glsl"

layout(set = 0, binding = 0) uniform sampler2D depthBuffer;

layout(std140, set = 0, binding = 1) uniform MotionVectorParams {
    mat4 invViewProjection;
    mat4 prevViewProjection;
    vec4 screenParams;      // xy = resolution, zw = 1/resolution
};

layout(set = 0, binding = 2, rg16f) uniform writeonly image2D motionVectorImage;

void main()
{
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 resolution = ivec2(screenParams.xy);

    if (pixel.x >= resolution.x || pixel.y >= resolution.y)
        return;

    vec2 uv = (vec2(pixel) + 0.5) * screenParams.zw;
    float depth = texture(depthBuffer, uv).r;

    // Sky pixels (depth at far plane) get zero motion vectors
    if (depth >= 1.0)
    {
        imageStore(motionVectorImage, pixel, vec4(0.0, 0.0, 0.0, 0.0));
        return;
    }

    // Reconstruct world position from depth
    vec4 clipPos = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 worldPos4 = invViewProjection * clipPos;
    vec3 worldPos = worldPos4.xyz / worldPos4.w;

    // Current NDC is simply derived from the pixel UV
    vec2 currentNDC = uv * 2.0 - 1.0;

    // Project world position with previous frame's VP to get previous NDC
    vec4 prevClip = prevViewProjection * vec4(worldPos, 1.0);
    vec2 prevNDC = prevClip.xy / prevClip.w;

    // Motion vector in NDC space (Streamline mvecScale handles conversion)
    vec2 mv = currentNDC - prevNDC;

    imageStore(motionVectorImage, pixel, vec4(mv, 0.0, 0.0));
}
