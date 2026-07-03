#type COMPUTE
#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_ray_query : require

#include "rt_shadow_trace_common.glsl"

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

// Set 0: TLAS (shared with the directional RT path, VK-1150)
layout(set = 0, binding = 0) uniform accelerationStructureEXT topLevelAS;

// Set 1: Inputs (same layout as rt_shadow.glsl so the pipeline UBO/sampler setup is reused)
layout(set = 1, binding = 0) uniform sampler2D depthBuffer;
layout(set = 1, binding = 1) uniform sampler2D normalBuffer;
layout(std140, set = 1, binding = 2) uniform RTShadowParams {
    mat4 invViewProjection;
    vec4 screenParams;      // xy = resolution, zw = 1/resolution
    vec4 cameraPosition;    // xyz = camera pos, w = far plane
};

// Set 2: Output. The pipeline binds a single-layer (e2D) view onto the chosen array slice,
// so the shader writes a plain image2D and never needs the slice index for addressing.
layout(set = 2, binding = 0, r8) uniform image2D shadowMaskSlice;

// Push constants: the light this dispatch traces toward. Shared by spot (VK-1175) and point
// (VK-1176) lights — a point light is a spot light with the cone disabled via the sentinel
// cosOuterAngle = -2.0 (any value < -1, since the cone cosine can never go below -1).
layout(push_constant) uniform PushConstants {
    vec4 lightPosition;     // xyz = world position, w = range/radius (ray tMax cap)
    vec4 lightDirection;    // xyz = spot axis (normalized), w = cosOuterAngle (-2.0 => point, no cone)
    vec4 biasParams;        // x = normal bias, y = ray t_min, z = cosInnerAngle, w = unused
};

vec3 reconstructWorldPos(vec2 uv, float depth) {
    vec4 clip = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 world = invViewProjection * clip;
    return world.xyz / world.w;
}

void main() {
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 screenSize = ivec2(screenParams.xy);

    if (pixel.x >= screenSize.x || pixel.y >= screenSize.y)
        return;

    vec2 uv = (vec2(pixel) + 0.5) * screenParams.zw;
    float depth = texture(depthBuffer, uv).r;

    // Sky pixels: fully lit (matches VSM "no shadow" outside the light volume).
    if (depth >= 1.0 || depth <= 0.0) {
        imageStore(shadowMaskSlice, pixel, vec4(1.0));
        return;
    }

    vec3 worldPos = reconstructWorldPos(uv, depth);
    vec3 toLight = lightPosition.xyz - worldPos;
    float dist = length(toLight);
    vec3 rayDir = toLight / max(dist, 1e-4);

    // Range/radius early-out: pixels beyond the light's reach are unshadowed by this light.
    if (dist > lightPosition.w) {
        imageStore(shadowMaskSlice, pixel, vec4(1.0));
        return;
    }

    // Cone test (spot lights only). Point lights pass cosOuterAngle = -2.0, which disables the cone
    // path entirely; coneT stays 1.0 so the final mix is a no-op and the result matches a bare ray.
    float coneT = 1.0;
    if (lightDirection.w > -1.5) {
        // Cone early-out: angle between the spot axis and the (light -> fragment) direction.
        // -rayDir points from the light toward the fragment; compare its cosine to cosOuterAngle.
        float cosAngle = dot(normalize(-rayDir), normalize(lightDirection.xyz));
        if (cosAngle < lightDirection.w) {
            imageStore(shadowMaskSlice, pixel, vec4(1.0));
            return;
        }
        // Smooth cone falloff (outer -> inner): computed here, applied after the ray trace below.
        coneT = smoothstep(lightDirection.w, biasParams.z, cosAngle); // 0 at outer, 1 at inner
    }

    vec3 N = normalize(texture(normalBuffer, uv).xyz);
    vec3 biasedPos = worldPos + N * biasParams.x;

    float tMin = biasParams.y;
    // Stop the ray at the light, not beyond it: geometry behind the light must not occlude.
    float tMax = max(dist - tMin, tMin);

    float shadow = traceRTShadowRay(topLevelAS,
        gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT,
        biasedPos, tMin, rayDir, tMax);

    // Fade the shadow back to "lit" at the cone edge so the RT result transitions seamlessly into the
    // VSM/unlit region just outside the cone. coneT == 1.0 for point lights, so this is a no-op there.
    shadow = mix(1.0, shadow, coneT);

    imageStore(shadowMaskSlice, pixel, vec4(shadow));
}
