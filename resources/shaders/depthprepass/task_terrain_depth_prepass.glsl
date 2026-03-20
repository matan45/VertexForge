#type TASK
#version 460 core
#extension GL_EXT_mesh_shader : require
#extension GL_GOOGLE_include_directive : require

#include "../common/gpu_types.glsl"
#include "../common/camera_types.glsl"

layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;

const uint TASK_WORKGROUP_SIZE = 32;
const uint MAX_MESHLETS_PER_PAYLOAD = 512;

layout(set = 0, binding = 0) uniform CameraUBO {
    CameraData camera;
};

layout(std430, set = 11, binding = 0) readonly buffer TerrainTileBuffer {
    TerrainTileGPUData tiles[];
};

layout(std430, set = 3, binding = 0) readonly buffer MeshletBuffer {
    GPUMeshlet meshlets[];
};

layout(push_constant) uniform PushConstants {
    uint tileCount;
    uint viewMode;
    float screenWidth;
    float screenHeight;
} pc;

struct TerrainMeshletPayload {
    uint tileIndex;
    uint lodLevel;
    uint baseVertexOffset;
    uint meshletIndices[MAX_MESHLETS_PER_PAYLOAD];
    uint meshletCount;
};

taskPayloadSharedEXT TerrainMeshletPayload payload;

shared uint sharedVisibleCount;
shared uint sharedMeshletIndices[MAX_MESHLETS_PER_PAYLOAD];
shared uint sharedTileData[5];

bool aabbInFrustum(vec3 aabbMin, vec3 aabbMax, vec4 frustumPlanes[6]) {
    for (int i = 0; i < 6; i++) {
        vec3 positive = vec3(
            frustumPlanes[i].x > 0.0 ? aabbMax.x : aabbMin.x,
            frustumPlanes[i].y > 0.0 ? aabbMax.y : aabbMin.y,
            frustumPlanes[i].z > 0.0 ? aabbMax.z : aabbMin.z
        );
        float distance = dot(frustumPlanes[i].xyz, positive) + frustumPlanes[i].w;
        if (distance < 0.0) {
            return false;
        }
    }
    return true;
}

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

void main() {
    uint tileIndex = gl_WorkGroupID.x;

    if (gl_LocalInvocationID.x == 0) {
        sharedVisibleCount = 0;
    }
    barrier();

    if (tileIndex >= pc.tileCount) {
        if (gl_LocalInvocationID.x == 0) {
            payload.meshletCount = 0;
            EmitMeshTasksEXT(0, 1, 1);
        }
        return;
    }

    TerrainTileGPUData tile = tiles[tileIndex];

    uint meshletOffset = 0;
    uint meshletCount = 0;
    uint baseVertexOffset = 0;
    bool tileVisible = true;

    if (gl_LocalInvocationID.x == 0) {
        // Frustum culling
        tileVisible = aabbInFrustum(tile.aabbMin.xyz, tile.aabbMax.xyz, camera.frustumPlanes);

        if (tileVisible) {
            // Force LOD3 (coarsest) for depth prepass - use the last LOD data
            uvec4 meshletData = getTerrainLODMeshletData(tile, 3);
            meshletOffset = meshletData.x;
            meshletCount = meshletData.y;
            baseVertexOffset = meshletData.z;

            // Fallback to any available LOD if LOD3 is empty
            if (meshletCount == 0) {
                for (uint lod = 2; lod < 4; lod--) {
                    meshletData = getTerrainLODMeshletData(tile, lod);
                    if (meshletData.y > 0) {
                        meshletOffset = meshletData.x;
                        meshletCount = meshletData.y;
                        baseVertexOffset = meshletData.z;
                        break;
                    }
                }
            }
        }
    }

    barrier();
    if (gl_LocalInvocationID.x == 0) {
        sharedTileData[0] = tileVisible ? 1 : 0;
        sharedTileData[1] = 3; // LOD3
        sharedTileData[2] = meshletOffset;
        sharedTileData[3] = meshletCount;
        sharedTileData[4] = baseVertexOffset;
    }
    barrier();

    tileVisible = sharedTileData[0] != 0;
    uint selectedLOD = sharedTileData[1];
    meshletOffset = sharedTileData[2];
    meshletCount = sharedTileData[3];
    baseVertexOffset = sharedTileData[4];

    if (gl_LocalInvocationID.x == 0) {
        sharedVisibleCount = 0;
    }
    barrier();

    if (!tileVisible || meshletCount == 0) {
        if (gl_LocalInvocationID.x == 0) {
            payload.meshletCount = 0;
            EmitMeshTasksEXT(0, 1, 1);
        }
        return;
    }

    // Meshlet-level frustum culling
    uint meshletsPerThread = (meshletCount + TASK_WORKGROUP_SIZE - 1) / TASK_WORKGROUP_SIZE;

    for (uint i = 0; i < meshletsPerThread; i++) {
        uint localMeshletIndex = gl_LocalInvocationID.x + i * TASK_WORKGROUP_SIZE;

        if (localMeshletIndex < meshletCount) {
            uint globalMeshletIndex = meshletOffset + localMeshletIndex;
            GPUMeshlet meshlet = meshlets[globalMeshletIndex];
            vec4 worldSphere = transformBoundingSphere(meshlet.boundingSphere, tile.modelMatrix);

            bool meshletVisible = sphereInFrustum(worldSphere, camera.frustumPlanes);

            if (meshletVisible) {
                uint slot = atomicAdd(sharedVisibleCount, 1);
                if (slot < MAX_MESHLETS_PER_PAYLOAD) {
                    sharedMeshletIndices[slot] = globalMeshletIndex;
                }
            }
        }
    }

    barrier();

    if (gl_LocalInvocationID.x == 0) {
        uint visibleCount = min(sharedVisibleCount, MAX_MESHLETS_PER_PAYLOAD);

        payload.tileIndex = tileIndex;
        payload.lodLevel = selectedLOD;
        payload.baseVertexOffset = baseVertexOffset;
        payload.meshletCount = visibleCount;

        for (uint i = 0; i < visibleCount; i++) {
            payload.meshletIndices[i] = sharedMeshletIndices[i];
        }

        EmitMeshTasksEXT(visibleCount, 1, 1);
    }
}
