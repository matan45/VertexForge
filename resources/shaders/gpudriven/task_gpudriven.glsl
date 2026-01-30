#type TASK
#version 460 core
#extension GL_EXT_mesh_shader : require
#extension GL_GOOGLE_include_directive : require

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
    // Cluster DAG mode fields (VK-293)
    uint clusterMode;       // 0 = discrete LOD, 1 = cluster DAG
    uint clusterBaseIndex;  // Base index into selection buffer
    uint clusterCount;      // Number of clusters to process
    uint padding;           // Alignment
} pc;

const uint MESHLET_CULL_FRUSTUM_BIT = 0x100u;
const uint MESHLET_CULL_BACKFACE_BIT = 0x200u;

struct MeshletPayload {
    uint drawIndex;
    uint meshletIndices[MAX_MESHLETS_PER_PAYLOAD];
    uint meshletCount;
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
// Discrete LOD Path - processes meshlets from PerDrawData ranges
// =========================================================================
void processDiscreteLOD() {
    uint drawIndex = pc.baseDrawIndex + gl_DrawID;
    PerDrawData drawData = perDrawData[drawIndex];

    uint localMeshletIndex = gl_LocalInvocationID.x;
    uint workgroupMeshletBase = gl_WorkGroupID.x * TASK_WORKGROUP_SIZE;
    uint meshletIndex = workgroupMeshletBase + localMeshletIndex;

    if (gl_LocalInvocationID.x == 0) {
        sharedVisibleCount = 0;
    }
    barrier();

    bool isValidMeshlet = meshletIndex < drawData.meshletCount;
    bool isVisible = false;

    if (isValidMeshlet) {
        uint globalMeshletIndex = drawData.meshletOffset + meshletIndex;
        GPUMeshlet meshlet = meshlets[globalMeshletIndex];
        vec4 worldSphere = transformBoundingSphere(meshlet.boundingSphere, drawData.modelMatrix);

        atomicAdd(stats.totalMeshlets, 1);
        isVisible = true;

        if ((pc.viewMode & MESHLET_CULL_FRUSTUM_BIT) != 0u) {
            bool frustumVisible = sphereInFrustum(worldSphere, camera.frustumPlanes);
            if (!frustumVisible) {
                atomicAdd(stats.culledByFrustum, 1);
                isVisible = false;
            }
        }

        if (isVisible && (pc.viewMode & MESHLET_CULL_BACKFACE_BIT) != 0u) {
            bool backfaceVisible = coneCullTest(meshlet.cone, drawData.modelMatrix,
                                                camera.cameraPos, worldSphere.xyz);
            if (!backfaceVisible) {
                atomicAdd(stats.culledByBackface, 1);
                isVisible = false;
            }
        }

        if (isVisible) {
            atomicAdd(stats.visibleMeshlets, 1);
            uint slot = atomicAdd(sharedVisibleCount, 1);
            if (slot < TASK_WORKGROUP_SIZE) {
                sharedMeshletIndices[slot] = globalMeshletIndex;
            }
        }
    }

    barrier();

    if (gl_LocalInvocationID.x == 0) {
        uint visibleCount = min(sharedVisibleCount, MAX_MESHLETS_PER_PAYLOAD);
        payload.drawIndex = drawIndex;
        payload.meshletCount = visibleCount;

        for (uint i = 0; i < visibleCount; i++) {
            payload.meshletIndices[i] = sharedMeshletIndices[i];
        }

        EmitMeshTasksEXT(visibleCount, 1, 1);
    }
}

// =========================================================================
// Cluster DAG Path - processes clusters from DAG traversal selection buffer
// =========================================================================
void processClusterDAG() {
    // Each workgroup processes one cluster from the selection buffer
    uint selectionIdx = pc.clusterBaseIndex + gl_WorkGroupID.x;

    // Bounds check - emit empty if beyond cluster count
    if (selectionIdx >= pc.clusterCount) {
        if (gl_LocalInvocationID.x == 0) {
            payload.meshletCount = 0;
            EmitMeshTasksEXT(0, 1, 1);
        }
        return;
    }

    // Load cluster selection from DAG traversal output
    GPUClusterSelection selection = selections[selectionIdx];
    uint globalClusterIdx = selection.clusterIndex;
    uint drawIndex = selection.padding;  // drawIndex stored in padding field

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

        for (uint i = 0; i < visibleCount; i++) {
            payload.meshletIndices[i] = sharedMeshletIndices[i];
        }

        EmitMeshTasksEXT(visibleCount, 1, 1);
    }
}

// =========================================================================
// Main Entry Point
// =========================================================================
void main() {
    if (pc.clusterMode == 0u) {
        // Discrete LOD path - traditional per-object meshlet processing
        processDiscreteLOD();
    } else {
        // Cluster DAG path - process clusters from DAG traversal
        processClusterDAG();
    }
}
