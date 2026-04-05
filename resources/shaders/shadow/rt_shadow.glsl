#type COMPUTE
#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_ray_query : require

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

// Set 0: TLAS
layout(set = 0, binding = 0) uniform accelerationStructureEXT topLevelAS;

// Set 1: Inputs
layout(set = 1, binding = 0) uniform sampler2D depthBuffer;
layout(set = 1, binding = 1) uniform sampler2D normalBuffer;
layout(std140, set = 1, binding = 2) uniform RTShadowParams {
    mat4 invViewProjection;
    vec4 screenParams;      // xy = resolution, zw = 1/resolution
    vec4 cameraPosition;    // xyz = camera pos, w = far plane
};

// Set 2: Output
layout(set = 2, binding = 0, r8) uniform image2D shadowMask;

// Push constants
layout(push_constant) uniform PushConstants {
    vec4 lightDirection;    // xyz = toward-light direction (normalized), w = max ray distance
    vec4 biasParams;        // x = normal bias, y = ray t_min, zw = unused
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

    // Sky pixels: fully lit
    if (depth >= 1.0 || depth <= 0.0) {
        imageStore(shadowMask, pixel, vec4(1.0));
        return;
    }

    vec3 worldPos = reconstructWorldPos(uv, depth);
    vec3 N = normalize(texture(normalBuffer, uv).xyz);

    // Apply normal bias to prevent self-shadowing
    vec3 biasedPos = worldPos + N * biasParams.x;

    // Shadow ray toward the light
    vec3 rayDir = normalize(lightDirection.xyz);
    float tMin = biasParams.y;
    float tMax = lightDirection.w;

    // Trace shadow ray using ray query
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

    imageStore(shadowMask, pixel, vec4(shadow));
}
