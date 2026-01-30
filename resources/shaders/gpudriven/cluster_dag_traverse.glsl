#type COMPUTE
#version 450
#extension GL_GOOGLE_include_directive : require

// =========================================================================
// Cluster DAG Traversal Compute Shader (VK-291, VK-292)
//
// Processes work queue entries, performing frustum culling, Hi-Z occlusion
// culling, and screen-space error evaluation to select clusters or enqueue
// children for further traversal. Uses ping-pong buffers for multi-pass
// traversal. Culling prunes entire subtrees for early-out optimization.
// =========================================================================

#include "../common/gpu_types.glsl"
#include "../common/camera_types.glsl"
#include "../common/cluster_types.glsl"

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

// =========================================================================
// Buffer Bindings
// =========================================================================

layout(std430, set = 0, binding = 0) readonly buffer ObjectBuffer {
    GPUObjectData objects[];
};

layout(set = 0, binding = 1) uniform CameraUBO {
    GPUCameraData camera;
};

layout(set = 0, binding = 5) uniform sampler2D hiZTexture;

layout(std430, set = 0, binding = 6) readonly buffer ClusterBuffer {
    GPUCluster clusters[];
};

layout(std430, set = 0, binding = 7) readonly buffer ClusterChildBuffer {
    GPUClusterChildren clusterChildren[];
};

layout(std430, set = 0, binding = 8) readonly buffer DAGHeaderBuffer {
    GPUClusterDAGHeader dagHeaders[];
};

layout(std430, set = 0, binding = 9) readonly buffer TraversalParamsBuffer {
    GPUClusterTraversalParams params;
};

layout(std430, set = 0, binding = 10) buffer TraversalStateBuffer {
    GPUDAGTraversalState state;
};

layout(std430, set = 0, binding = 11) buffer WorkQueueA {
    uint workQueueA[];
};

layout(std430, set = 0, binding = 12) buffer WorkQueueB {
    uint workQueueB[];
};

layout(std430, set = 0, binding = 13) writeonly buffer SelectionBuffer {
    GPUClusterSelection selections[];
};

// Object to draw index mapping - filled by gpu_cull_lod.glsl (VK-293)
layout(std430, set = 0, binding = 14) readonly buffer ObjectDrawIndexMap {
    uint objectDrawIndexMap[];
};

// =========================================================================
// Helper Functions
// =========================================================================

vec4 transformBoundingSphere(vec4 localSphere, mat4 modelMatrix) {
    vec3 worldCenter = (modelMatrix * vec4(localSphere.xyz, 1.0)).xyz;
    float scaleX = length(modelMatrix[0].xyz);
    float scaleY = length(modelMatrix[1].xyz);
    float scaleZ = length(modelMatrix[2].xyz);
    float maxScale = max(max(scaleX, scaleY), scaleZ);
    float worldRadius = localSphere.w * maxScale;
    return vec4(worldCenter, worldRadius);
}

bool sphereInFrustum(vec4 sphere, vec4 frustumPlanes[6]) {
    for (int i = 0; i < 6; i++) {
        float distance = dot(frustumPlanes[i].xyz, sphere.xyz) + frustumPlanes[i].w;
        if (distance < -sphere.w) {
            return false;
        }
    }
    return true;
}

// Hi-Z occlusion test - returns true if potentially visible
bool hiZOcclusionTest(vec4 worldSphere, mat4 viewProjection, vec2 screenSize, uint hiZMipLevels) {
    vec3 center = worldSphere.xyz;
    float radius = worldSphere.w;
    vec3 aabbMin = center - vec3(radius);
    vec3 aabbMax = center + vec3(radius);

    // Project 8 corners of AABB to clip space
    vec4 corners[8];
    corners[0] = viewProjection * vec4(aabbMin.x, aabbMin.y, aabbMin.z, 1.0);
    corners[1] = viewProjection * vec4(aabbMax.x, aabbMin.y, aabbMin.z, 1.0);
    corners[2] = viewProjection * vec4(aabbMin.x, aabbMax.y, aabbMin.z, 1.0);
    corners[3] = viewProjection * vec4(aabbMax.x, aabbMax.y, aabbMin.z, 1.0);
    corners[4] = viewProjection * vec4(aabbMin.x, aabbMin.y, aabbMax.z, 1.0);
    corners[5] = viewProjection * vec4(aabbMax.x, aabbMin.y, aabbMax.z, 1.0);
    corners[6] = viewProjection * vec4(aabbMin.x, aabbMax.y, aabbMax.z, 1.0);
    corners[7] = viewProjection * vec4(aabbMax.x, aabbMax.y, aabbMax.z, 1.0);

    // Find NDC bounds and minimum depth
    vec2 ndcMin = vec2(1.0);
    vec2 ndcMax = vec2(-1.0);
    float minDepth = 1.0;

    for (int i = 0; i < 8; i++) {
        // If any corner is behind the camera, assume visible
        if (corners[i].w <= 0.0) {
            return true;
        }
        vec3 ndc = corners[i].xyz / corners[i].w;
        ndcMin = min(ndcMin, ndc.xy);
        ndcMax = max(ndcMax, ndc.xy);
        minDepth = min(minDepth, ndc.z);
    }

    // Clamp to valid NDC range
    ndcMin = clamp(ndcMin, vec2(-1.0), vec2(1.0));
    ndcMax = clamp(ndcMax, vec2(-1.0), vec2(1.0));

    // If behind near plane, assume visible
    if (minDepth < 0.0) {
        return true;
    }

    // Convert NDC to UV coordinates
    vec2 uvMin = ndcMin * 0.5 + 0.5;
    vec2 uvMax = ndcMax * 0.5 + 0.5;

    // Select mip level based on projected size
    vec2 sizePixels = (uvMax - uvMin) * screenSize;
    float maxDimension = max(sizePixels.x, sizePixels.y);
    float mipLevel = ceil(log2(maxDimension));
    mipLevel = clamp(mipLevel, 0.0, float(hiZMipLevels - 1u));

    // Sample Hi-Z at 4 corners and take maximum depth
    float hiZDepth = 0.0;
    hiZDepth = max(hiZDepth, textureLod(hiZTexture, uvMin, mipLevel).r);
    hiZDepth = max(hiZDepth, textureLod(hiZTexture, uvMax, mipLevel).r);
    hiZDepth = max(hiZDepth, textureLod(hiZTexture, vec2(uvMin.x, uvMax.y), mipLevel).r);
    hiZDepth = max(hiZDepth, textureLod(hiZTexture, vec2(uvMax.x, uvMin.y), mipLevel).r);

    // Visible if object's minimum depth is in front of or at Hi-Z depth
    return minDepth <= hiZDepth + 0.0001;
}

// Pack object index and local cluster index into single uint
// Format: (objectIndex << 20) | (localClusterIndex & 0xFFFFF)
// Supports up to 4096 objects and 1M clusters per object
uint packWorkItem(uint objectIndex, uint localClusterIndex) {
    return (objectIndex << 20) | (localClusterIndex & 0xFFFFFu);
}

void unpackWorkItem(uint packed, out uint objectIndex, out uint localClusterIndex) {
    objectIndex = packed >> 20;
    localClusterIndex = packed & 0xFFFFFu;
}

// =========================================================================
// Main Entry Point
// =========================================================================

void main() {
    uint workIdx = gl_GlobalInvocationID.x;

    // Early exit if beyond queue size
    if (workIdx >= state.inputQueueCount) {
        return;
    }

    // Read work item from appropriate queue based on pass index (ping-pong)
    uint packed;
    if ((state.passIndex % 2u) == 0u) {
        packed = workQueueA[workIdx];
    } else {
        packed = workQueueB[workIdx];
    }

    // Unpack work item
    uint objectIndex;
    uint localClusterIdx;
    unpackWorkItem(packed, objectIndex, localClusterIdx);

    // Validate object index
    if (objectIndex >= camera.objectCount) {
        return;
    }

    // Load object data
    GPUObjectData obj = objects[objectIndex];

    // Verify this is a DAG object
    if (!usesClusterDAG(obj)) {
        return;
    }

    // Get DAG header for this object
    uint dagHeaderIdx = getDagHeaderIndex(obj);
    GPUClusterDAGHeader dagHeader = dagHeaders[dagHeaderIdx];

    // Compute global cluster index
    uint globalClusterIdx = dagHeader.clusterOffset + localClusterIdx;

    // Load cluster data
    GPUCluster cluster = clusters[globalClusterIdx];
    GPUClusterChildren children = clusterChildren[globalClusterIdx];

    // Transform bounding sphere to world space
    vec4 worldSphere = transformBoundingSphere(cluster.boundingSphere, obj.modelMatrix);

    // =========================================================================
    // Frustum Culling - prunes entire subtree if outside frustum
    // =========================================================================
    if (params.enableCulling != 0u) {
        if (!sphereInFrustum(worldSphere, camera.frustumPlanes)) {
            atomicAdd(state.totalSubtreesCulled, 1u);
            return;
        }
    }

    // =========================================================================
    // Hi-Z Occlusion Culling - prunes entire subtree if occluded (VK-292)
    // =========================================================================
    if (params.enableOcclusion != 0u) {
        if (camera.hiZMipLevels > 0u) {
            if (!hiZOcclusionTest(worldSphere, camera.viewProjection,
                                  camera.screenParams.xy, camera.hiZMipLevels)) {
                atomicAdd(state.totalSubtreesCulled, 1u);
                return;
            }
        }
    }

    // =========================================================================
    // Screen-Space Error Calculation
    // =========================================================================
    float distance = length(worldSphere.xyz - camera.cameraPosition.xyz);
    float screenError = computeClusterScreenError(
        cluster.geometricError,
        distance,
        params.projectionFactor
    );

    // Unpack cluster level and flags
    uint level;
    uint flags;
    unpackClusterLevelFlags(cluster.levelFlagsPacked, level, flags);
    bool isLeaf = isClusterLeaf(flags);

    // =========================================================================
    // Selection Decision
    // =========================================================================
    bool shouldSelect = isLeaf || shouldSelectCluster(
        screenError,
        params.screenErrorThreshold,
        params.errorMultiplier
    );

    if (shouldSelect) {
        // SELECT this cluster - add to selection buffer
        uint slot = atomicAdd(state.selectedCount, 1u);

        if (slot < params.maxClustersToSelect) {
            selections[slot].clusterIndex = globalClusterIdx;
            selections[slot].isSelected = 1u;
            selections[slot].screenError = screenError;
            // Store drawIndex (not objectIndex) for task shader's PerDrawData lookup
            selections[slot].padding = objectDrawIndexMap[objectIndex];
        }

        atomicAdd(state.totalSelected, 1u);
    } else {
        // TRAVERSE to children - enqueue to output queue for next pass

        // Enqueue left child if present
        if (children.leftChild != INVALID_CLUSTER_INDEX) {
            uint queueIdx = atomicAdd(state.outputQueueCount, 1u);

            // Bounds check to prevent buffer overflow
            if (queueIdx < params.maxWorkQueueEntries) {
                uint outPacked = packWorkItem(objectIndex, children.leftChild);

                // Write to opposite queue (ping-pong)
                if ((state.passIndex % 2u) == 0u) {
                    workQueueB[queueIdx] = outPacked;
                } else {
                    workQueueA[queueIdx] = outPacked;
                }
            }
        }

        // Enqueue right child if present
        if (children.rightChild != INVALID_CLUSTER_INDEX) {
            uint queueIdx = atomicAdd(state.outputQueueCount, 1u);

            // Bounds check to prevent buffer overflow
            if (queueIdx < params.maxWorkQueueEntries) {
                uint outPacked = packWorkItem(objectIndex, children.rightChild);

                if ((state.passIndex % 2u) == 0u) {
                    workQueueB[queueIdx] = outPacked;
                } else {
                    workQueueA[queueIdx] = outPacked;
                }
            }
        }
    }

    atomicAdd(state.totalProcessed, 1u);
}
