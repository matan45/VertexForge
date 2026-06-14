#type COMPUTE
#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_ray_query : require

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

// Push constants: the spot light this dispatch traces toward.
layout(push_constant) uniform PushConstants {
    vec4 lightPosition;     // xyz = world position, w = range (ray tMax cap)
    vec4 lightDirection;    // xyz = spot axis (normalized, points down the cone), w = cosOuterAngle
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

    // Range early-out: pixels beyond the light's reach are unshadowed by this light.
    if (dist > lightPosition.w) {
        imageStore(shadowMaskSlice, pixel, vec4(1.0));
        return;
    }

    // Cone early-out: angle between the spot axis and the (light -> fragment) direction.
    // -rayDir points from the light toward the fragment; compare its cosine to cosOuterAngle.
    float cosAngle = dot(normalize(-rayDir), normalize(lightDirection.xyz));
    if (cosAngle < lightDirection.w) {
        imageStore(shadowMaskSlice, pixel, vec4(1.0));
        return;
    }

    vec3 N = normalize(texture(normalBuffer, uv).xyz);
    vec3 biasedPos = worldPos + N * biasParams.x;

    float tMin = biasParams.y;
    // Stop the ray at the light, not beyond it: geometry behind the light must not occlude.
    float tMax = max(dist - tMin, tMin);

    rayQueryEXT rq;
    rayQueryInitializeEXT(rq, topLevelAS,
        gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT,
        0xFF,
        biasedPos,
        tMin,
        rayDir,
        tMax);

    while (rayQueryProceedEXT(rq)) {}

    float shadow = 1.0; // fully lit
    if (rayQueryGetIntersectionTypeEXT(rq, true) == gl_RayQueryCommittedIntersectionTriangleEXT) {
        shadow = 0.0; // occluded
    }

    // Smooth cone falloff (outer -> inner): fade the shadow back to "lit" at the cone edge so the
    // RT result transitions seamlessly into the VSM/unlit region just outside the cone.
    float coneT = smoothstep(lightDirection.w, biasParams.z, cosAngle); // 0 at outer, 1 at inner
    shadow = mix(1.0, shadow, coneT);

    imageStore(shadowMaskSlice, pixel, vec4(shadow));
}
