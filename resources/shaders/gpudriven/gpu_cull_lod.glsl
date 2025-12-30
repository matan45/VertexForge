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

// Object flags (must match ObjectFlags namespace in GPUDrivenTypes.hpp)
const uint FLAG_ALPHA_MASK    = 1u << 4;
const uint FLAG_NO_CULL       = 1u << 6;
const uint FLAG_NO_OCCLUDE    = 1u << 7;
const uint FLAG_UNIFORM_SCALE = 1u << 9;

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
    uint availableLODMask;      // 4 bytes - bits 0-3: which LODs are ready for streaming
    uint shaderGroupIndex;      // 4 bytes - 0 = default PBR, 1+ = custom shaders
};

// ============================================================================
// Per-Draw Data (224 bytes, must match PerDrawData in GPUDrivenTypes.hpp)
// ============================================================================
struct PerDrawData {
    mat4 modelMatrix;           // 64 bytes
    mat4 normalMatrix;          // 64 bytes - pre-computed transpose(inverse(mat3(model)))

    vec4 albedo;                // 16 bytes
    vec4 materialParams;        // 16 bytes

    uvec4 textureIndices0;      // 16 bytes
    uvec4 textureIndices1;      // 16 bytes

    uint objectIndex;           // 4 bytes
    uint flags;                 // 4 bytes
    float iblDiffuse;           // 4 bytes
    float iblSpecular;          // 4 bytes

    uint lodLevel;              // 4 bytes - selected LOD (for debug)
    uint shaderGroupIndex;      // 4 bytes - 0 = default PBR, 1+ = custom shaders
    uint padding1;              // 4 bytes
    uint padding2;              // 4 bytes
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
    uint batchCount;            // Number of indirect draw batches

    uint commandsPerBatch;      // Max draw commands per batch (total, for backwards compat)
    uint shaderGroupCount;      // Number of shader groups (buffer sections per batch)
    uint padding1;
    uint padding2;
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

// Helper: commands per (batch, shaderGroup) section
uint getCommandsPerSection() {
    return camera.commandsPerBatch / camera.shaderGroupCount;
}

// Helper: calculate section index for (batch, shaderGroup)
uint getSectionIndex(uint batch, uint shaderGroup) {
    return batch * camera.shaderGroupCount + shaderGroup;
}

// Output: Draw commands
layout(std430, set = 0, binding = 2) writeonly buffer DrawCommandBuffer {
    DrawCommand drawCommands[];
};

// Output: Per-draw data (for vertex/fragment shaders)
layout(std430, set = 0, binding = 3) writeonly buffer PerDrawDataBuffer {
    PerDrawData perDrawData[];
};

// Per-batch statistics (must match BatchDrawStats in GPUDrivenTypes.hpp)
// Aligned to 32 bytes for GPU efficiency
struct BatchDrawStats {
    uint drawCount;          // Number of visible objects in this batch
    uint lodCount0;          // Objects using LOD0
    uint lodCount1;          // Objects using LOD1
    uint lodCount2;          // Objects using LOD2
    uint lodCount3;          // Objects using LOD3
    uint culledByFrustum;    // Objects culled by frustum
    uint culledByOcclusion;  // Objects culled by Hi-Z occlusion
    uint padding;            // Padding to 32 bytes
};

// Output: Atomic draw count and statistics per section (batch, shaderGroup)
// Layout: [Batch0_Group0][Batch0_Group1]...[BatchN_GroupM]
// Section index = batch * shaderGroupCount + shaderGroup
layout(std430, set = 0, binding = 4) buffer DrawCountBuffer {
    BatchDrawStats batchStats[];  // One per section (batchCount * shaderGroupCount)
};

// Input: Hi-Z pyramid texture (for occlusion culling)
layout(set = 0, binding = 5) uniform sampler2D hiZTexture;

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
    // For perspective projection, the projected size is: radius * |proj[1][1]| / w
    // Note: Use abs() because Vulkan projection matrices have negative Y for Y-flip
    float projectedRadius = worldSphere.w * abs(projection[1][1]) / clipPos.w;

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

// Find the best available LOD given streaming constraints
// Returns 0xFFFFFFFF if no LOD is available
uint findBestAvailableLOD(uint targetLOD, uint availableMask) {
    // If all LODs are available (mask = 0xF or 15), just use target
    if (availableMask == 0xFu) {
        return targetLOD;
    }

    // If no LODs are available, return invalid
    if (availableMask == 0u) {
        return 0xFFFFFFFFu;
    }

    // First try to find a LOD >= target (prefer lower quality if target not ready)
    for (uint lod = targetLOD; lod < 4u; ++lod) {
        if ((availableMask & (1u << lod)) != 0u) {
            return lod;
        }
    }

    // Fallback to any available LOD (prefer higher quality / lower index)
    for (uint lod = 0u; lod < 4u; ++lod) {
        if ((availableMask & (1u << lod)) != 0u) {
            return lod;
        }
    }

    return 0xFFFFFFFFu; // No LOD available (shouldn't reach here)
}

// Project bounding sphere to screen-space AABB and test against Hi-Z pyramid
// Returns true if object is visible (not occluded)
bool hiZOcclusionTest(vec4 worldSphere, mat4 viewProjection, vec2 screenSize, uint hiZMipLevels) {
    vec3 center = worldSphere.xyz;
    float radius = worldSphere.w;

    // Calculate 8 corners of world-space AABB from bounding sphere
    vec3 aabbMin = center - vec3(radius);
    vec3 aabbMax = center + vec3(radius);

    // Project all 8 corners to clip space
    vec4 corners[8];
    corners[0] = viewProjection * vec4(aabbMin.x, aabbMin.y, aabbMin.z, 1.0);
    corners[1] = viewProjection * vec4(aabbMax.x, aabbMin.y, aabbMin.z, 1.0);
    corners[2] = viewProjection * vec4(aabbMin.x, aabbMax.y, aabbMin.z, 1.0);
    corners[3] = viewProjection * vec4(aabbMax.x, aabbMax.y, aabbMin.z, 1.0);
    corners[4] = viewProjection * vec4(aabbMin.x, aabbMin.y, aabbMax.z, 1.0);
    corners[5] = viewProjection * vec4(aabbMax.x, aabbMin.y, aabbMax.z, 1.0);
    corners[6] = viewProjection * vec4(aabbMin.x, aabbMax.y, aabbMax.z, 1.0);
    corners[7] = viewProjection * vec4(aabbMax.x, aabbMax.y, aabbMax.z, 1.0);

    // Find screen-space AABB of projected corners
    vec2 ndcMin = vec2(1.0);
    vec2 ndcMax = vec2(-1.0);
    float minDepth = 1.0;

    for (int i = 0; i < 8; i++) {
        // Handle behind-camera case
        if (corners[i].w <= 0.0) {
            // Some part of AABB is behind camera - consider visible
            return true;
        }

        // Perspective divide
        vec3 ndc = corners[i].xyz / corners[i].w;

        // Update screen-space bounds
        ndcMin = min(ndcMin, ndc.xy);
        ndcMax = max(ndcMax, ndc.xy);

        // Track minimum depth (closest point)
        // Vulkan uses [0, 1] depth range
        minDepth = min(minDepth, ndc.z);
    }

    // Clamp to valid screen range
    ndcMin = clamp(ndcMin, vec2(-1.0), vec2(1.0));
    ndcMax = clamp(ndcMax, vec2(-1.0), vec2(1.0));

    // If object is completely behind near plane
    if (minDepth < 0.0) {
        return true; // Visible (touching near plane)
    }

    // Convert NDC to UV [0, 1]
    vec2 uvMin = ndcMin * 0.5 + 0.5;
    vec2 uvMax = ndcMax * 0.5 + 0.5;

    // Calculate screen-space size in pixels
    vec2 sizePixels = (uvMax - uvMin) * screenSize;
    float maxDimension = max(sizePixels.x, sizePixels.y);

    // Select Hi-Z mip level based on projected size
    // We want to sample a mip where one texel covers approximately the AABB
    float mipLevel = ceil(log2(maxDimension));
    mipLevel = clamp(mipLevel, 0.0, float(hiZMipLevels - 1u));

    // Sample Hi-Z at 4 corners of the screen-space AABB
    // Use the maximum depth from all samples (conservative)
    float hiZDepth = 0.0;
    hiZDepth = max(hiZDepth, textureLod(hiZTexture, uvMin, mipLevel).r);
    hiZDepth = max(hiZDepth, textureLod(hiZTexture, uvMax, mipLevel).r);
    hiZDepth = max(hiZDepth, textureLod(hiZTexture, vec2(uvMin.x, uvMax.y), mipLevel).r);
    hiZDepth = max(hiZDepth, textureLod(hiZTexture, vec2(uvMax.x, uvMin.y), mipLevel).r);

    // Object is occluded if its nearest point is behind the Hi-Z depth
    // Add small epsilon to avoid precision issues
    return minDepth <= hiZDepth + 0.0001;
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

    // Transform bounding sphere to world space
    vec4 worldSphere = transformBoundingSphere(obj.boundingSphere, obj.modelMatrix);

    // Calculate batch (round-robin) and section (batch * shaderGroupCount + shaderGroup)
    uint batchIndex = objectIndex % camera.batchCount;
    uint shaderGroup = obj.shaderGroupIndex;
    uint sectionIndex = getSectionIndex(batchIndex, shaderGroup);
    uint commandsPerSection = getCommandsPerSection();

    // ========================================
    // Frustum Culling
    // ========================================
    if (camera.enableFrustumCulling != 0u && (obj.flags & FLAG_NO_CULL) == 0u) {
        if (!sphereInFrustum(worldSphere, camera.frustumPlanes)) {
            atomicAdd(batchStats[sectionIndex].culledByFrustum, 1);
            return; // Outside frustum, skip this object
        }
    }

    // ========================================
    // Hi-Z Occlusion Culling
    // ========================================
    if (camera.enableOcclusionCulling != 0u && (obj.flags & FLAG_NO_OCCLUDE) == 0u) {
        if (camera.hiZMipLevels > 0u) {
            if (!hiZOcclusionTest(worldSphere, camera.viewProjection, camera.screenParams.xy, camera.hiZMipLevels)) {
                atomicAdd(batchStats[sectionIndex].culledByOcclusion, 1);
                return; // Occluded by Hi-Z, skip this object
            }
        }
    }

    // ========================================
    // LOD Selection (with Streaming Support)
    // ========================================
    uint targetLOD = 0;

    if (camera.enableLODSelection != 0u) {
        // Transform sphere to view space for accurate projection
        vec4 viewSphere = camera.view * vec4(worldSphere.xyz, 1.0);
        viewSphere.w = worldSphere.w; // Keep radius

        float screenPixels = projectSphereToScreen(viewSphere, camera.projection, camera.screenParams.xy);
        targetLOD = selectLOD(screenPixels, obj.lodThresholds);
    }

    // Find best available LOD considering streaming state
    // availableLODMask bits: bit 0 = LOD0 ready, bit 1 = LOD1 ready, etc.
    uint lodLevel = findBestAvailableLOD(targetLOD, obj.availableLODMask);

    // Skip object if no LOD is available (mesh still streaming)
    if (lodLevel == 0xFFFFFFFFu) {
        return;
    }

    // Get LOD geometry data
    uvec4 lodData = getLODData(obj, lodLevel);
    uint indexCount = lodData.z;
    uint vertexCount = lodData.w;

    // Additional validation: fallback to lower LODs if geometry data is invalid
    // (This handles edge cases where LOD is marked ready but has no geometry)
    while (lodLevel > 0u && (indexCount == 0u || vertexCount == 0u)) {
        lodLevel--;
        // Check if this lower LOD is available
        if ((obj.availableLODMask & (1u << lodLevel)) == 0u) {
            continue;  // This LOD not available, try next
        }
        lodData = getLODData(obj, lodLevel);
        indexCount = lodData.z;
        vertexCount = lodData.w;
    }

    // Skip if no valid LOD has geometry
    if (indexCount == 0u) {
        return;
    }

    // ========================================
    // Emit Draw Command (Section-Aware: batch x shaderGroup)
    // ========================================

    // Atomically allocate a draw slot within this section
    uint localDrawIndex = atomicAdd(batchStats[sectionIndex].drawCount, 1);

    // Bounds check - if section is full, decrement and skip
    if (localDrawIndex >= commandsPerSection) {
        atomicAdd(batchStats[sectionIndex].drawCount, uint(-1));
        return;
    }

    // Track LOD distribution statistics per section
    switch (lodLevel) {
        case 0u: atomicAdd(batchStats[sectionIndex].lodCount0, 1); break;
        case 1u: atomicAdd(batchStats[sectionIndex].lodCount1, 1); break;
        case 2u: atomicAdd(batchStats[sectionIndex].lodCount2, 1); break;
        default: atomicAdd(batchStats[sectionIndex].lodCount3, 1); break;
    }

    // Calculate global indices into combined buffers
    // Layout: [Batch0_Group0][Batch0_Group1]...[BatchN_GroupM]
    uint globalDrawIndex = sectionIndex * commandsPerSection + localDrawIndex;

    // Write draw command at global index
    drawCommands[globalDrawIndex].indexCount = indexCount;
    drawCommands[globalDrawIndex].instanceCount = 1;
    drawCommands[globalDrawIndex].firstIndex = lodData.y;   // indexOffset
    drawCommands[globalDrawIndex].vertexOffset = int(lodData.x);  // vertexOffset
    drawCommands[globalDrawIndex].firstInstance = globalDrawIndex; // For fetching per-draw data

    // Write per-draw data at global index (for vertex/fragment shaders)
    perDrawData[globalDrawIndex].modelMatrix = obj.modelMatrix;

    // Pre-compute normal matrix (transpose of inverse of upper-left 3x3)
    // Done once per object here instead of per-vertex in the vertex shader
    mat3 modelMat3 = mat3(obj.modelMatrix);
    mat3 normalMat3;

    if ((obj.flags & FLAG_UNIFORM_SCALE) != 0u) {
        // Fast path: for uniform scale, normalMatrix = modelMatrix / scale
        // This avoids expensive inverse() computation (~27 ops)
        float scale = length(modelMat3[0]);
        normalMat3 = modelMat3 * (1.0 / scale);
    } else {
        // General case: full inverse for non-uniform scale
        normalMat3 = transpose(inverse(modelMat3));
    }

    perDrawData[globalDrawIndex].normalMatrix = mat4(normalMat3);

    perDrawData[globalDrawIndex].albedo = obj.albedo;
    perDrawData[globalDrawIndex].materialParams = obj.materialParams;
    perDrawData[globalDrawIndex].textureIndices0 = obj.textureIndices0;
    perDrawData[globalDrawIndex].textureIndices1 = obj.textureIndices1;
    perDrawData[globalDrawIndex].objectIndex = objectIndex;
    perDrawData[globalDrawIndex].flags = obj.flags;
    perDrawData[globalDrawIndex].iblDiffuse = obj.iblParams.x;
    perDrawData[globalDrawIndex].iblSpecular = obj.iblParams.y;
    perDrawData[globalDrawIndex].lodLevel = lodLevel;
    perDrawData[globalDrawIndex].shaderGroupIndex = obj.shaderGroupIndex;
    perDrawData[globalDrawIndex].padding1 = 0u;
    perDrawData[globalDrawIndex].padding2 = 0u;
}
