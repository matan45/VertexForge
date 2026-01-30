#type TASK
#version 460 core
#extension GL_EXT_mesh_shader : require
#extension GL_GOOGLE_include_directive : require
#extension GL_ARB_shader_draw_parameters : require

#include "../common/gpu_types.glsl"
#include "../common/camera_types.glsl"
#include "../common/cluster_types.glsl"

layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;

const uint TASK_WORKGROUP_SIZE = 32;
const uint MAX_MESHLETS_PER_PAYLOAD = 32;

layout(set = 0, binding = 0) uniform CameraUBO {
    CameraData camera;
};

layout(std430, set = 1, binding = 0) readonly buffer PerDrawDataBuffer {
    PerDrawData perDrawData[];
};

layout(std430, set = 3, binding = 0) readonly buffer MeshletBuffer {
    GPUMeshlet meshlets[];
};

// Must match MeshletCullingStats in MeshShaderPipeline.hpp
layout(std430, set = 3, binding = 3) buffer CullingStatsBuffer {
    uint totalMeshlets;
    uint culledByFrustum;
    uint culledByBackface;
    uint visibleMeshlets;
} stats;

// Cluster DAG buffers (VK-293)
layout(std430, set = 3, binding = 4) readonly buffer ClusterBuffer {
    GPUCluster clusters[];
};

layout(std430, set = 3, binding = 5) readonly buffer ClusterSelectionBuffer {
    GPUClusterSelection selections[];
};

layout(push_constant) uniform PushConstants {
    uint baseDrawIndex;
    uint viewMode;
    float screenWidth;
    float screenHeight;
    // VK-300: clusterMode removed - always DAG mode now
    uint clusterBaseIndex;  // Base index into selection buffer
    uint clusterCount;      // Number of clusters to process
    uint padding1;          // Alignment
    uint padding2;          // Alignment (replaces removed clusterMode)
} pc;

const uint MESHLET_CULL_FRUSTUM_BIT = 0x100u;
const uint MESHLET_CULL_BACKFACE_BIT = 0x200u;

struct MeshletPayload {
    uint drawIndex;
    uint meshletIndices[MAX_MESHLETS_PER_PAYLOAD];
    uint meshletCount;

    // VK-298: Debug visualization fields (set per-cluster in DAG mode)
    uint debugClusterIndex;      // Unique cluster ID for visualization
    uint debugClusterLevel;      // DAG hierarchy depth (0 = leaf, higher = coarser)
    float debugScreenError;      // Screen-space error from LOD selection
    uint debugStreamingState;    // 0=not loaded, 1=loading, 2=loaded
};

taskPayloadSharedEXT MeshletPayload payload;

shared uint sharedVisibleCount;
shared uint sharedMeshletIndices[TASK_WORKGROUP_SIZE];

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

bool coneCullTest(vec4 cone, mat4 modelMatrix, vec3 cameraPos, vec3 meshletCenter) {
    if (cone.w >= 1.0) {
        return true;
    }
    vec3 worldConeAxis = normalize(mat3(modelMatrix) * cone.xyz);
    vec3 viewDir = normalize(meshletCenter - cameraPos);
    float dotProduct = dot(viewDir, worldConeAxis);
    return dotProduct < cone.w;
}

// =========================================================================
// Direct Meshlet Path - processes meshlets directly from PerDrawData
// Used when cluster data isn't available (fallback mode)
// =========================================================================

void processDirectMeshlet() {
    // Each draw processes meshlets for one object
    // gl_DrawID identifies which draw command (0 to N-1) we're processing
    // gl_WorkGroupID.x is the workgroup within that draw (always 0 since we dispatch 1 workgroup per draw)
    uint drawIndex = pc.baseDrawIndex + gl_DrawID;

    // Load draw data
    PerDrawData drawData = perDrawData[drawIndex];

    // Skip if no meshlets
    if (drawData.meshletCount == 0u) {
        if (gl_LocalInvocationID.x == 0) {
            payload.meshletCount = 0;
            EmitMeshTasksEXT(0, 1, 1);
        }
        return;
    }

    // Initialize shared memory
    if (gl_LocalInvocationID.x == 0) {
        sharedVisibleCount = 0;
    }
    barrier();

    // Process meshlets within this draw call
    uint meshletCount = min(drawData.meshletCount, TASK_WORKGROUP_SIZE);
    uint localMeshletIdx = gl_LocalInvocationID.x;

    if (localMeshletIdx < meshletCount) {
        uint globalMeshletIdx = drawData.meshletOffset + localMeshletIdx;
        GPUMeshlet meshlet = meshlets[globalMeshletIdx];

        vec4 meshletWorldSphere = transformBoundingSphere(meshlet.boundingSphere, drawData.modelMatrix);
        bool visible = true;

        atomicAdd(stats.totalMeshlets, 1);

        // Frustum cull
        if ((pc.viewMode & MESHLET_CULL_FRUSTUM_BIT) != 0u) {
            visible = sphereInFrustum(meshletWorldSphere, camera.frustumPlanes);
            if (!visible) {
                atomicAdd(stats.culledByFrustum, 1);
            }
        }

        // Backface cone cull
        if (visible && (pc.viewMode & MESHLET_CULL_BACKFACE_BIT) != 0u) {
            visible = coneCullTest(meshlet.cone, drawData.modelMatrix,
                                   camera.cameraPos, meshletWorldSphere.xyz);
            if (!visible) {
                atomicAdd(stats.culledByBackface, 1);
            }
        }

        if (visible) {
            atomicAdd(stats.visibleMeshlets, 1);
            uint slot = atomicAdd(sharedVisibleCount, 1);
            if (slot < MAX_MESHLETS_PER_PAYLOAD) {
                sharedMeshletIndices[slot] = globalMeshletIdx;
            }
        }
    }

    barrier();

    // Emit payload to mesh shader
    if (gl_LocalInvocationID.x == 0) {
        uint visibleCount = min(sharedVisibleCount, MAX_MESHLETS_PER_PAYLOAD);
        payload.drawIndex = drawIndex;
        payload.meshletCount = visibleCount;

        // No DAG data in direct mode - use defaults
        payload.debugClusterIndex = 0u;
        payload.debugClusterLevel = 0u;
        payload.debugScreenError = 0.0;
        payload.debugStreamingState = 2u;  // Loaded

        for (uint i = 0; i < visibleCount; i++) {
            payload.meshletIndices[i] = sharedMeshletIndices[i];
        }

        EmitMeshTasksEXT(visibleCount, 1, 1);
    }
}

// =========================================================================
// Cluster DAG Path - processes clusters from DAG traversal selection buffer
// =========================================================================

// SECURITY: Maximum selection buffer size - must match MAX_CLUSTER_SELECTIONS_PER_FRAME in ClusterBufferTypes.hpp
const uint MAX_SELECTION_BUFFER_SIZE = 1024u * 1024u;

void processClusterDAG() {
    // Each workgroup processes one cluster from the selection buffer
    uint selectionIdx = pc.clusterBaseIndex + gl_WorkGroupID.x;

    // SECURITY: Bounds check against BOTH user-supplied count AND actual buffer size
    // This prevents buffer overrun if clusterBaseIndex is maliciously large
    // CPU-side validation should also ensure: clusterBaseIndex + clusterCount <= MAX_SELECTION_BUFFER_SIZE
    if (selectionIdx >= pc.clusterCount || selectionIdx >= MAX_SELECTION_BUFFER_SIZE) {
        if (gl_LocalInvocationID.x == 0) {
            payload.meshletCount = 0;
            EmitMeshTasksEXT(0, 1, 1);
        }
        return;
    }

    // Load cluster selection from DAG traversal output
    GPUClusterSelection selection = selections[selectionIdx];
    uint globalClusterIdx = selection.clusterIndex;
    // VK-298: Unpack drawIndex (lower 24 bits) and streaming state (upper 8 bits)
    uint drawIndex = selection.padding & 0xFFFFFFu;
    uint streamingState = selection.padding >> 24;

    // Validate selection
    if (selection.isSelected == 0u) {
        if (gl_LocalInvocationID.x == 0) {
            payload.meshletCount = 0;
            EmitMeshTasksEXT(0, 1, 1);
        }
        return;
    }

    // Load cluster data
    GPUCluster cluster = clusters[globalClusterIdx];

    // Get draw data for this cluster's object
    PerDrawData drawData = perDrawData[drawIndex];

    // Initialize shared memory
    if (gl_LocalInvocationID.x == 0) {
        sharedVisibleCount = 0;
    }
    barrier();

    // Unpack cluster meshlet info
    uint meshletCount;
    uint triangleCount;
    unpackClusterCounts(cluster.meshletTrianglePacked, meshletCount, triangleCount);

    // Transform cluster bounds for culling
    vec4 worldSphere = transformBoundingSphere(cluster.boundingSphere, drawData.modelMatrix);

    // CLUSTER-LEVEL BACKFACE CONE CULLING
    // If entire cluster is backfacing, skip all its meshlets
    if ((pc.viewMode & MESHLET_CULL_BACKFACE_BIT) != 0u) {
        if (!coneCullTest(cluster.cone, drawData.modelMatrix, camera.cameraPos, worldSphere.xyz)) {
            // Entire cluster is backfacing
            if (gl_LocalInvocationID.x == 0) {
                payload.meshletCount = 0;
                EmitMeshTasksEXT(0, 1, 1);
            }
            return;
        }
    }

    // PROCESS MESHLETS WITHIN CLUSTER
    // Each thread in workgroup processes one meshlet
    uint localMeshletIdx = gl_LocalInvocationID.x;

    if (localMeshletIdx < meshletCount && localMeshletIdx < TASK_WORKGROUP_SIZE) {
        uint globalMeshletIdx = cluster.meshletOffset + localMeshletIdx;
        GPUMeshlet meshlet = meshlets[globalMeshletIdx];

        vec4 meshletWorldSphere = transformBoundingSphere(meshlet.boundingSphere, drawData.modelMatrix);
        bool visible = true;

        atomicAdd(stats.totalMeshlets, 1);

        // Frustum cull individual meshlet
        if ((pc.viewMode & MESHLET_CULL_FRUSTUM_BIT) != 0u) {
            visible = sphereInFrustum(meshletWorldSphere, camera.frustumPlanes);
            if (!visible) {
                atomicAdd(stats.culledByFrustum, 1);
            }
        }

        // Meshlet-level cone cull
        if (visible && (pc.viewMode & MESHLET_CULL_BACKFACE_BIT) != 0u) {
            visible = coneCullTest(meshlet.cone, drawData.modelMatrix,
                                   camera.cameraPos, meshletWorldSphere.xyz);
            if (!visible) {
                atomicAdd(stats.culledByBackface, 1);
            }
        }

        if (visible) {
            atomicAdd(stats.visibleMeshlets, 1);
            uint slot = atomicAdd(sharedVisibleCount, 1);
            if (slot < MAX_MESHLETS_PER_PAYLOAD) {
                sharedMeshletIndices[slot] = globalMeshletIdx;
            }
        }
    }

    barrier();

    // Emit payload to mesh shader
    if (gl_LocalInvocationID.x == 0) {
        uint visibleCount = min(sharedVisibleCount, MAX_MESHLETS_PER_PAYLOAD);
        payload.drawIndex = drawIndex;
        payload.meshletCount = visibleCount;

        // VK-298: Set debug visualization data from cluster
        payload.debugClusterIndex = globalClusterIdx;
        uint clusterLevel, clusterFlags;
        unpackClusterLevelFlags(cluster.levelFlagsPacked, clusterLevel, clusterFlags);
        payload.debugClusterLevel = clusterLevel;
        payload.debugScreenError = selection.screenError;
        payload.debugStreamingState = streamingState;  // From packed selection.padding

        for (uint i = 0; i < visibleCount; i++) {
            payload.meshletIndices[i] = sharedMeshletIndices[i];
        }

        EmitMeshTasksEXT(visibleCount, 1, 1);
    }
}

// =========================================================================
// Main Entry Point
// Selects between DAG cluster mode and direct meshlet mode based on clusterCount
// =========================================================================
void main() {
    if (pc.clusterCount > 0u) {
        // DAG cluster rendering mode - process clusters from selection buffer
        processClusterDAG();
    } else {
        // Direct meshlet rendering mode - fallback when cluster data not available
        processDirectMeshlet();
    }
}
