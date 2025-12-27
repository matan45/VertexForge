#type COMPUTE
#version 450

// Hi-Z Occlusion Culling Compute Shader
// Tests AABBs against the Hi-Z pyramid and outputs visibility flags

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

// Occlusion flags (must match OcclusionFlags in OcclusionCullingManager.hpp)
const uint FLAG_NONE        = 0u;
const uint FLAG_NO_OCCLUDE  = 1u << 1;  // Object should never occlude others
const uint FLAG_NO_CULL     = 1u << 2;  // Object should never be culled (always visible)

layout(set = 0, binding = 0) uniform sampler2D hiZPyramid;

// World-space AABBs
struct ObjectData {
    vec4 aabbMin;  // xyz = min corner, w = entityId
    vec4 aabbMax;  // xyz = max corner, w = flags
    mat4 modelMatrix;
};

layout(std430, set = 0, binding = 1) readonly buffer ObjectBuffer {
    ObjectData objects[];
};

// Output: Visibility flags (1 = visible, 0 = occluded)
layout(std430, set = 0, binding = 2) writeonly buffer VisibilityBuffer {
    uint visibility[];
};

layout(set = 0, binding = 3) uniform CameraUBO {
    mat4 viewProj;
    vec4 screenSize;  // xy = width/height, zw = 1/width, 1/height
    float nearPlane;
    uint objectCount;
    uint hiZMipLevels;
    uint padding;
};

// Project a point to screen space, returns NDC (xy in [-1,1], z = depth)
vec4 projectPoint(vec3 worldPos) {
    vec4 clip = viewProj * vec4(worldPos, 1.0);
    return clip;
}

bool testAABBVisible(vec3 aabbMin, vec3 aabbMax) {
    vec3 corners[8];
    corners[0] = vec3(aabbMin.x, aabbMin.y, aabbMin.z);
    corners[1] = vec3(aabbMax.x, aabbMin.y, aabbMin.z);
    corners[2] = vec3(aabbMin.x, aabbMax.y, aabbMin.z);
    corners[3] = vec3(aabbMax.x, aabbMax.y, aabbMin.z);
    corners[4] = vec3(aabbMin.x, aabbMin.y, aabbMax.z);
    corners[5] = vec3(aabbMax.x, aabbMin.y, aabbMax.z);
    corners[6] = vec3(aabbMin.x, aabbMax.y, aabbMax.z);
    corners[7] = vec3(aabbMax.x, aabbMax.y, aabbMax.z);

    // Project corners and find screen-space bounds
    vec2 minScreen = vec2(1.0);
    vec2 maxScreen = vec2(0.0);
    float minDepth = 1.0;
    bool allBehind = true;

    for (int i = 0; i < 8; i++) {
        vec4 clip = projectPoint(corners[i]);

        // Check if in front of near plane
        if (clip.w > nearPlane) {
            allBehind = false;

            // Perspective divide
            vec3 ndc = clip.xyz / clip.w;

            // NDC to UV [0, 1]
            vec2 uv = ndc.xy * 0.5 + 0.5;

            minScreen = min(minScreen, uv);
            maxScreen = max(maxScreen, uv);
            minDepth = min(minDepth, ndc.z);
        }
    }

    // If all corners are behind camera, conservatively mark as visible
    // (object might straddle the near plane)
    if (allBehind) {
        return true;
    }

    // Objects extending off-screen should be conservatively marked as visible
    // Clamping their bounds would reduce the sample region and cause false occlusion
    if (minScreen.x < 0.0 || minScreen.y < 0.0 || maxScreen.x > 1.0 || maxScreen.y > 1.0) {
        return true;  // Partially off-screen, conservatively visible
    }

    // Calculate screen coverage to determine mip level
    vec2 screenSize2D = screenSize.xy;
    vec2 rectSize = (maxScreen - minScreen) * screenSize2D;
    float maxDim = max(rectSize.x, rectSize.y);

    // Don't auto-cull small objects - let them render (conservative approach)
    // Objects < 1 pixel may still contribute visually and should not be
    // culled without proper occlusion testing
    if (maxDim < 1.0) {
        return true;  // Conservative: keep small objects visible
    }

    // Select mip level based on coverage
    // Add +1 to ensure AABB covers at least 4 texels for more accurate sampling
    // (industry practice to avoid over-aggressive culling)
    float mipLevel = floor(log2(maxDim)) + 1.0;
    mipLevel = clamp(mipLevel, 0.0, float(hiZMipLevels - 1));

    // Sample Hi-Z at the center of the screen-space AABB
    vec2 centerUV = (minScreen + maxScreen) * 0.5;
    float hiZDepth = textureLod(hiZPyramid, centerUV, mipLevel).r;

    // Also sample corners for better coverage
    float hiZDepth00 = textureLod(hiZPyramid, minScreen, mipLevel).r;
    float hiZDepth10 = textureLod(hiZPyramid, vec2(maxScreen.x, minScreen.y), mipLevel).r;
    float hiZDepth01 = textureLod(hiZPyramid, vec2(minScreen.x, maxScreen.y), mipLevel).r;
    float hiZDepth11 = textureLod(hiZPyramid, maxScreen, mipLevel).r;

    // Take maximum (furthest) depth from Hi-Z samples
    float maxHiZDepth = max(max(max(hiZDepth00, hiZDepth10), max(hiZDepth01, hiZDepth11)), hiZDepth);

    // Object is visible if its closest depth is in front of the Hi-Z depth
    // Note: In standard Vulkan depth (0=near, 1=far), smaller = closer
    return minDepth <= maxHiZDepth;
}

void main() {
    uint objectIndex = gl_GlobalInvocationID.x;

    if (objectIndex >= objectCount) {
        return;
    }

    ObjectData obj = objects[objectIndex];

    // Extract flags from aabbMax.w (stored as float, reinterpret as uint)
    uint flags = floatBitsToUint(obj.aabbMax.w);

    // NO_CULL objects are always visible
    if ((flags & FLAG_NO_CULL) != 0u) {
        visibility[objectIndex] = 1u;
        return;
    }

    // Transform AABB to world space
    // For axis-aligned, we need to find the new AABB that encloses the transformed one
    vec3 localMin = obj.aabbMin.xyz;
    vec3 localMax = obj.aabbMax.xyz;

    // Transform all 8 corners and find new AABB
    vec3 worldMin = vec3(1e30);
    vec3 worldMax = vec3(-1e30);

    vec3 corners[8];
    corners[0] = vec3(localMin.x, localMin.y, localMin.z);
    corners[1] = vec3(localMax.x, localMin.y, localMin.z);
    corners[2] = vec3(localMin.x, localMax.y, localMin.z);
    corners[3] = vec3(localMax.x, localMax.y, localMin.z);
    corners[4] = vec3(localMin.x, localMin.y, localMax.z);
    corners[5] = vec3(localMax.x, localMin.y, localMax.z);
    corners[6] = vec3(localMin.x, localMax.y, localMax.z);
    corners[7] = vec3(localMax.x, localMax.y, localMax.z);

    for (int i = 0; i < 8; i++) {
        vec4 worldCorner = obj.modelMatrix * vec4(corners[i], 1.0);
        worldMin = min(worldMin, worldCorner.xyz);
        worldMax = max(worldMax, worldCorner.xyz);
    }

    // Test visibility against Hi-Z
    bool isVisible = testAABBVisible(worldMin, worldMax);

    visibility[objectIndex] = isVisible ? 1u : 0u;
}
