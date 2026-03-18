#ifndef VFX_LIGHTING_GLSL
#define VFX_LIGHTING_GLSL

// Simplified lighting evaluation for VFX particles.
// Uses Lambertian diffuse (no PBR metallic/roughness).
// Reuses light structures and cluster lookup from the main renderer.
//
// The including shader must declare descriptor sets 1-3 before including this:
//   Set 1: Light buffers (directionalLights, pointLights, spotLights, lightCounts)
//   Set 2: ClusterGridParams UBO
//   Set 3: ClusterLightGrid + LightIndexList SSBOs

#include "../common/lighting_functions.glsl"
#include "../common/cluster_culling.glsl"

// Normal mode constants
const uint VFX_NORMAL_SPHERE = 0u;
const uint VFX_NORMAL_VIEW_ALIGNED = 1u;
const uint VFX_NORMAL_MESH = 2u;

// Compute sphere normal from billboard UV (makes particle look spherical)
vec3 computeSphereNormal(vec2 uv, mat4 viewMatrix) {
    vec2 centered = uv * 2.0 - 1.0;
    float lenSq = dot(centered, centered);

    vec3 right   = vec3(viewMatrix[0][0], viewMatrix[1][0], viewMatrix[2][0]);
    vec3 up      = vec3(viewMatrix[0][1], viewMatrix[1][1], viewMatrix[2][1]);
    vec3 forward = -vec3(viewMatrix[0][2], viewMatrix[1][2], viewMatrix[2][2]);

    if (lenSq > 1.0) {
        return forward;
    }

    float z = sqrt(1.0 - lenSq);
    return normalize(right * centered.x + up * centered.y + forward * z);
}

#endif // VFX_LIGHTING_GLSL
