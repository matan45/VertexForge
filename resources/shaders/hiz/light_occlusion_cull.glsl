#type COMPUTE
#version 450

// Light Occlusion Culling Compute Shader
// Tests light bounding spheres against the Hi-Z pyramid and outputs visibility flags

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

// Light types
const uint LIGHT_TYPE_POINT = 0u;
const uint LIGHT_TYPE_SPOT  = 1u;

layout(set = 0, binding = 0) uniform sampler2D hiZPyramid;

// Light bounding data
struct LightBounds {
    vec4 positionRadius;  // xyz = world position, w = bounding radius
    vec4 direction;       // xyz = direction (spot only), w = cone angle (spot only)
    uint entityId;
    uint lightType;       // 0 = point, 1 = spot
    uint padding0;
    uint padding1;
};

layout(std430, set = 0, binding = 1) readonly buffer LightBoundsBuffer {
    LightBounds lights[];
};

// Output: Visibility flags (1 = visible, 0 = occluded)
layout(std430, set = 0, binding = 2) writeonly buffer VisibilityBuffer {
    uint visibility[];
};

layout(set = 0, binding = 3) uniform CameraUBO {
    mat4 viewProj;
    vec4 screenSize;   // xy = width/height, zw = 1/width, 1/height
    vec4 cameraPos;    // xyz = camera position, w = near plane
    uint lightCount;
    uint hiZMipLevels;
    uint padding0;
    uint padding1;
};

// Test if a sphere is visible against the Hi-Z buffer
// Uses AABB-based projection like the mesh culling shader for robustness
bool testSphereVisible(vec3 center, float radius) {
    // Build AABB from sphere
    vec3 aabbMin = center - vec3(radius);
    vec3 aabbMax = center + vec3(radius);

    // Project all 8 AABB corners to clip space
    vec4 corners[8];
    corners[0] = viewProj * vec4(aabbMin.x, aabbMin.y, aabbMin.z, 1.0);
    corners[1] = viewProj * vec4(aabbMax.x, aabbMin.y, aabbMin.z, 1.0);
    corners[2] = viewProj * vec4(aabbMin.x, aabbMax.y, aabbMin.z, 1.0);
    corners[3] = viewProj * vec4(aabbMax.x, aabbMax.y, aabbMin.z, 1.0);
    corners[4] = viewProj * vec4(aabbMin.x, aabbMin.y, aabbMax.z, 1.0);
    corners[5] = viewProj * vec4(aabbMax.x, aabbMin.y, aabbMax.z, 1.0);
    corners[6] = viewProj * vec4(aabbMin.x, aabbMax.y, aabbMax.z, 1.0);
    corners[7] = viewProj * vec4(aabbMax.x, aabbMax.y, aabbMax.z, 1.0);

    // Find screen-space bounds and minimum depth
    vec2 ndcMin = vec2(1.0);
    vec2 ndcMax = vec2(-1.0);
    float minDepth = 1.0;

    for (int i = 0; i < 8; i++) {
        // If any corner is behind camera, conservatively mark as visible
        if (corners[i].w <= 0.0) {
            return true;
        }
        vec3 ndc = corners[i].xyz / corners[i].w;
        ndcMin = min(ndcMin, ndc.xy);
        ndcMax = max(ndcMax, ndc.xy);
        minDepth = min(minDepth, ndc.z);
    }

    // Clamp to screen bounds
    ndcMin = clamp(ndcMin, vec2(-1.0), vec2(1.0));
    ndcMax = clamp(ndcMax, vec2(-1.0), vec2(1.0));

    // If in front of near plane, conservatively visible
    if (minDepth < 0.0) {
        return true;
    }

    // Convert NDC to UV [0, 1]
    vec2 uvMin = ndcMin * 0.5 + 0.5;
    vec2 uvMax = ndcMax * 0.5 + 0.5;

    // Calculate screen-space size to determine mip level
    vec2 sizePixels = (uvMax - uvMin) * screenSize.xy;
    float maxDimension = max(sizePixels.x, sizePixels.y);

    // Very small lights (< 1 pixel) - conservatively visible
    if (maxDimension < 1.0) {
        return true;
    }

    // Select mip level based on coverage
    float mipLevel = ceil(log2(maxDimension));
    mipLevel = clamp(mipLevel, 0.0, float(hiZMipLevels - 1));

    // Sample Hi-Z at the 4 corners of the screen-space bounds
    float hiZDepth = 0.0;
    hiZDepth = max(hiZDepth, textureLod(hiZPyramid, uvMin, mipLevel).r);
    hiZDepth = max(hiZDepth, textureLod(hiZPyramid, uvMax, mipLevel).r);
    hiZDepth = max(hiZDepth, textureLod(hiZPyramid, vec2(uvMin.x, uvMax.y), mipLevel).r);
    hiZDepth = max(hiZDepth, textureLod(hiZPyramid, vec2(uvMax.x, uvMin.y), mipLevel).r);

    // Sphere is visible if its nearest point is in front of (or at) the Hi-Z depth
    // In Vulkan: 0 = near, 1 = far, so smaller = closer
    return minDepth <= hiZDepth + 0.0001;
}

void main() {
    uint lightIndex = gl_GlobalInvocationID.x;

    if (lightIndex >= lightCount) {
        return;
    }

    LightBounds light = lights[lightIndex];

    vec3 position = light.positionRadius.xyz;
    float radius = light.positionRadius.w;

    // Test sphere visibility against Hi-Z
    bool isVisible = testSphereVisible(position, radius);

    visibility[lightIndex] = isVisible ? 1u : 0u;
}
