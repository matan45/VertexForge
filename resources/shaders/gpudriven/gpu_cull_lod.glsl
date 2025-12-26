#type COMPUTE
#version 450

// GPU-Driven Culling + LOD Selection Compute Shader
//
// This shader performs frustum culling and LOD selection on the GPU,
// outputting VkDrawIndexedIndirectCommand and PerDrawData for each visible object.
// The result is a compacted list of draw commands for indirect rendering.

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

// Constants (must match GPUDrivenTypes.hpp)
const uint INVALID_TEXTURE_INDEX = 0xFFFFFFFF;
const float LOD_THRESHOLD_0 = 400.0;
const float LOD_THRESHOLD_1 = 200.0;
const float LOD_THRESHOLD_2 = 100.0;

// Object flags (must match ObjectFlags namespace)
const uint FLAG_VISIBLE       = 1u << 0;
const uint FLAG_CAST_SHADOW   = 1u << 1;
const uint FLAG_RECEIVE_SHADOW = 1u << 2;
const uint FLAG_TRANSPARENT   = 1u << 3;
const uint FLAG_ALPHA_MASK    = 1u << 4;
const uint FLAG_DOUBLE_SIDED  = 1u << 5;
const uint FLAG_NO_CULL       = 1u << 6;
const uint FLAG_NO_OCCLUDE    = 1u << 7;
const uint FLAG_SELECTED      = 1u << 8;

// ============================================================================
// GPU Object Data (256 bytes, must match GPUObjectData in GPUDrivenTypes.hpp)
// ============================================================================
struct GPUObjectData {
    mat4 modelMatrix;           // 64 bytes

    vec4 boundingSphere;        // 16 bytes - xyz = center (local), w = radius

    uvec4 lod0Data;             // 16 bytes - vertexOffset, indexOffset, indexCount, vertexCount
    uvec4 lod1Data;             // 16 bytes
    uvec4 lod2Data;             // 16 bytes
    uvec4 lod3Data;             // 16 bytes

    vec4 lodThresholds;         // 16 bytes - threshold0, threshold1, threshold2, lodBias

    vec4 albedo;                // 16 bytes
    vec4 materialParams;        // 16 bytes - metallic, roughness, ao, emission
    vec4 iblParams;             // 16 bytes - iblDiffuse, iblSpecular, padding, padding

    uvec4 textureIndices0;      // 16 bytes - albedo, normal, orm, metallic
    uvec4 textureIndices1;      // 16 bytes - roughness, ao, emission, height

    uint flags;                 // 4 bytes
    uint entityId;              // 4 bytes
    uint padding0;              // 4 bytes
    uint padding1;              // 4 bytes
};

// ============================================================================
// Per-Draw Data (144 bytes, must match PerDrawData in GPUDrivenTypes.hpp)
// ============================================================================
struct PerDrawData {
    mat4 modelMatrix;           // 64 bytes

    vec4 albedo;                // 16 bytes
    vec4 materialParams;        // 16 bytes

    uvec4 textureIndices0;      // 16 bytes
    uvec4 textureIndices1;      // 16 bytes

    uint objectIndex;           // 4 bytes
    uint flags;                 // 4 bytes
    float iblDiffuse;           // 4 bytes
    float iblSpecular;          // 4 bytes
};

// ============================================================================
// VkDrawIndexedIndirectCommand (20 bytes)
// ============================================================================
struct DrawCommand {
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    int  vertexOffset;
    uint firstInstance;
};

// ============================================================================
// Camera Data (must match GPUCameraData in GPUDrivenTypes.hpp)
// ============================================================================
struct CameraData {
    mat4 view;
    mat4 projection;
    mat4 viewProjection;
    mat4 invViewProjection;

    vec4 cameraPosition;        // xyz = position, w = nearPlane
    vec4 screenParams;          // xy = resolution, zw = 1/resolution

    vec4 frustumPlanes[6];

    float farPlane;
    uint objectCount;
    uint hiZMipLevels;
    uint frameIndex;

    uint enableFrustumCulling;
    uint enableOcclusionCulling;
    uint enableLODSelection;
    uint padding;
};

// ============================================================================
// Descriptor Bindings
// ============================================================================

// Input: Object data
layout(std430, set = 0, binding = 0) readonly buffer ObjectBuffer {
    GPUObjectData objects[];
};

// Input: Camera/culling parameters
layout(set = 0, binding = 1) uniform CameraUBO {
    CameraData camera;
};

// Output: Draw commands
layout(std430, set = 0, binding = 2) writeonly buffer DrawCommandBuffer {
    DrawCommand drawCommands[];
};

// Output: Per-draw data (for vertex/fragment shaders)
layout(std430, set = 0, binding = 3) writeonly buffer PerDrawDataBuffer {
    PerDrawData perDrawData[];
};

// Output: Atomic draw count
layout(std430, set = 0, binding = 4) buffer DrawCountBuffer {
    uint drawCount;
};

// ============================================================================
// Helper Functions
// ============================================================================

// Get LOD data for a given level
uvec4 getLODData(GPUObjectData obj, uint level) {
    switch (level) {
        case 0: return obj.lod0Data;
        case 1: return obj.lod1Data;
        case 2: return obj.lod2Data;
        default: return obj.lod3Data;
    }
}

// Transform bounding sphere from local to world space
vec4 transformBoundingSphere(vec4 localSphere, mat4 modelMatrix) {
    // Transform center
    vec3 worldCenter = (modelMatrix * vec4(localSphere.xyz, 1.0)).xyz;

    // Approximate uniform scale from matrix (use max of axis lengths)
    float scaleX = length(modelMatrix[0].xyz);
    float scaleY = length(modelMatrix[1].xyz);
    float scaleZ = length(modelMatrix[2].xyz);
    float maxScale = max(max(scaleX, scaleY), scaleZ);

    // Scale radius
    float worldRadius = localSphere.w * maxScale;

    return vec4(worldCenter, worldRadius);
}

// Test sphere against frustum planes
bool sphereInFrustum(vec4 sphere, vec4 frustumPlanes[6]) {
    for (int i = 0; i < 6; i++) {
        // Distance from sphere center to plane
        float distance = dot(frustumPlanes[i].xyz, sphere.xyz) + frustumPlanes[i].w;

        // If sphere is completely behind this plane, it's outside frustum
        if (distance < -sphere.w) {
            return false;
        }
    }
    return true;
}

// Project sphere to screen and return diameter in pixels
float projectSphereToScreen(vec4 worldSphere, mat4 projection, vec2 screenSize) {
    // Project sphere center to clip space
    vec4 clipPos = projection * vec4(worldSphere.xyz, 1.0);

    // Handle behind camera
    if (clipPos.w <= 0.01) {
        return 10000.0; // Very close, use highest LOD
    }

    // Calculate screen-space radius using projection matrix
    // For perspective projection, the projected size is: radius * proj[1][1] / w
    float projectedRadius = worldSphere.w * projection[1][1] / clipPos.w;

    // Convert to pixels (projectedRadius is in NDC [-1, 1], so multiply by half screen height)
    float screenDiameter = projectedRadius * screenSize.y;

    return screenDiameter;
}

// Select LOD level based on screen-space size
uint selectLOD(float screenPixels, vec4 thresholds) {
    // Apply LOD bias (stored in thresholds.w)
    // Positive bias = lower quality (higher LOD level)
    // Negative bias = higher quality (lower LOD level)
    float adjustedPixels = screenPixels * pow(2.0, -thresholds.w);

    if (adjustedPixels > thresholds.x) return 0;  // LOD0 for >= threshold0
    if (adjustedPixels > thresholds.y) return 1;  // LOD1 for >= threshold1
    if (adjustedPixels > thresholds.z) return 2;  // LOD2 for >= threshold2
    return 3;                                      // LOD3 for < threshold2
}

// ============================================================================
// Main
// ============================================================================
void main() {
    uint objectIndex = gl_GlobalInvocationID.x;

    // Bounds check
    if (objectIndex >= camera.objectCount) {
        return;
    }

    GPUObjectData obj = objects[objectIndex];

    // Skip if object is flagged as transparent (handled separately on CPU)
    if ((obj.flags & FLAG_TRANSPARENT) != 0u) {
        return;
    }

    // Transform bounding sphere to world space
    vec4 worldSphere = transformBoundingSphere(obj.boundingSphere, obj.modelMatrix);

    // ========================================
    // Frustum Culling
    // ========================================
    if (camera.enableFrustumCulling != 0u && (obj.flags & FLAG_NO_CULL) == 0u) {
        if (!sphereInFrustum(worldSphere, camera.frustumPlanes)) {
            return; // Outside frustum, skip this object
        }
    }

    // ========================================
    // LOD Selection
    // ========================================
    uint lodLevel = 0;

    if (camera.enableLODSelection != 0u) {
        // Transform sphere to view space for accurate projection
        vec4 viewSphere = camera.view * vec4(worldSphere.xyz, 1.0);
        viewSphere.w = worldSphere.w; // Keep radius

        float screenPixels = projectSphereToScreen(viewSphere, camera.projection, camera.screenParams.xy);
        lodLevel = selectLOD(screenPixels, obj.lodThresholds);
    }

    // Get LOD geometry data
    uvec4 lodData = getLODData(obj, lodLevel);
    uint indexCount = lodData.z;

    // Skip if this LOD has no geometry
    if (indexCount == 0u) {
        return;
    }

    // ========================================
    // Emit Draw Command
    // ========================================

    // Atomically allocate a draw slot
    uint drawIndex = atomicAdd(drawCount, 1);

    // Write draw command
    drawCommands[drawIndex].indexCount = indexCount;
    drawCommands[drawIndex].instanceCount = 1;
    drawCommands[drawIndex].firstIndex = lodData.y;   // indexOffset
    drawCommands[drawIndex].vertexOffset = int(lodData.x);  // vertexOffset
    drawCommands[drawIndex].firstInstance = drawIndex; // For fetching per-draw data

    // Write per-draw data (for vertex/fragment shaders)
    perDrawData[drawIndex].modelMatrix = obj.modelMatrix;
    perDrawData[drawIndex].albedo = obj.albedo;
    perDrawData[drawIndex].materialParams = obj.materialParams;
    perDrawData[drawIndex].textureIndices0 = obj.textureIndices0;
    perDrawData[drawIndex].textureIndices1 = obj.textureIndices1;
    perDrawData[drawIndex].objectIndex = objectIndex;
    perDrawData[drawIndex].flags = obj.flags;
    perDrawData[drawIndex].iblDiffuse = obj.iblParams.x;
    perDrawData[drawIndex].iblSpecular = obj.iblParams.y;
}
