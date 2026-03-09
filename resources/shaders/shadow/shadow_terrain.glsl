#type TASK
#version 460 core
#extension GL_EXT_mesh_shader : require
#extension GL_GOOGLE_include_directive : require

#include "../common/gpu_types.glsl"

layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;

const uint TASK_WORKGROUP_SIZE = 32;
const uint MAX_MESHLETS_PER_PAYLOAD = 512;

layout(push_constant) uniform TerrainShadowPushConstants {
    mat4 lightViewProjection;
    uint tileCount;
    uint shadowLOD;         // Which LOD to use for shadows (0-3)
    float depthBias;
    float slopeBias;
    float normalBias;       // Normal offset to prevent self-shadowing at coarser LODs
    float _pad0;
    float _pad1;
    float _pad2;
} pc;

layout(std430, set = 0, binding = 0) readonly buffer TerrainTileBuffer {
    TerrainTileGPUData tiles[];
};

layout(std430, set = 1, binding = 0) readonly buffer MeshletBuffer {
    GPUMeshlet meshlets[];
};

struct TerrainShadowPayload {
    uint tileIndex;
    uint lodLevel;
    uint baseVertexOffset;
    uint meshletIndices[MAX_MESHLETS_PER_PAYLOAD];
    uint meshletCount;
};

taskPayloadSharedEXT TerrainShadowPayload payload;

shared uint sharedVisibleCount;
shared uint sharedMeshletIndices[MAX_MESHLETS_PER_PAYLOAD];
shared vec4 sharedFrustumPlanes[6];
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

bool sphereInFrustum(vec4 sphere, vec4 frustumPlanes[6]) {
    for (int i = 0; i < 6; i++) {
        float distance = dot(frustumPlanes[i].xyz, sphere.xyz) + frustumPlanes[i].w;
        if (distance < -sphere.w) {
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

// Find best available LOD (for streaming support)
// Uses mainMeshletCount (.w) to check availability - shadows only render surface meshlets
uint findBestAvailableLOD(TerrainTileGPUData tile, uint targetLOD) {
    uvec4 data = getTerrainLODMeshletData(tile, targetLOD);
    if (data.w > 0) return targetLOD;

    // Try coarser LODs first for shadows (prefer lower detail)
    for (uint lod = targetLOD; lod < 4; lod++) {
        uvec4 lodData = getTerrainLODMeshletData(tile, lod);
        if (lodData.w > 0) return lod;
    }

    // Try finer LODs as fallback
    for (uint lod = 0; lod < targetLOD; lod++) {
        uvec4 lodData = getTerrainLODMeshletData(tile, lod);
        if (lodData.w > 0) return lod;
    }

    return targetLOD;
}

void main() {
    uint tileIndex = gl_WorkGroupID.x;

    // Initialize shared memory and compute frustum planes
    if (gl_LocalInvocationID.x == 0) {
        sharedVisibleCount = 0;

        // Extract frustum planes from light view-projection matrix
        mat4 vp = pc.lightViewProjection;
        sharedFrustumPlanes[0] = vec4(vp[0][3] + vp[0][0], vp[1][3] + vp[1][0], vp[2][3] + vp[2][0], vp[3][3] + vp[3][0]); // Left
        sharedFrustumPlanes[1] = vec4(vp[0][3] - vp[0][0], vp[1][3] - vp[1][0], vp[2][3] - vp[2][0], vp[3][3] - vp[3][0]); // Right
        sharedFrustumPlanes[2] = vec4(vp[0][3] + vp[0][1], vp[1][3] + vp[1][1], vp[2][3] + vp[2][1], vp[3][3] + vp[3][1]); // Bottom
        sharedFrustumPlanes[3] = vec4(vp[0][3] - vp[0][1], vp[1][3] - vp[1][1], vp[2][3] - vp[2][1], vp[3][3] - vp[3][1]); // Top
        sharedFrustumPlanes[4] = vec4(vp[0][3] + vp[0][2], vp[1][3] + vp[1][2], vp[2][3] + vp[2][2], vp[3][3] + vp[3][2]); // Near
        sharedFrustumPlanes[5] = vec4(vp[0][3] - vp[0][2], vp[1][3] - vp[1][2], vp[2][3] - vp[2][2], vp[3][3] - vp[3][2]); // Far

        // Normalize planes
        const float PLANE_NORMALIZE_EPSILON = 0.0001;
        for (int i = 0; i < 6; i++) {
            float len = max(length(sharedFrustumPlanes[i].xyz), PLANE_NORMALIZE_EPSILON);
            sharedFrustumPlanes[i] /= len;
        }
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

    // Thread 0 does tile-level culling and LOD selection
    uint selectedLOD = 0;
    uint meshletOffset = 0;
    uint meshletCount = 0;
    uint baseVertexOffset = 0;
    bool tileVisible = true;

    if (gl_LocalInvocationID.x == 0) {
        // Tile-level frustum culling against light frustum
        tileVisible = aabbInFrustum(tile.aabbMin.xyz, tile.aabbMax.xyz, sharedFrustumPlanes);

        if (tileVisible) {
            // Use fixed LOD for shadows (configurable, default to coarse)
            uint targetLOD = min(pc.shadowLOD, 3u);
            selectedLOD = findBestAvailableLOD(tile, targetLOD);

            uvec4 meshletData = getTerrainLODMeshletData(tile, selectedLOD);
            meshletOffset = meshletData.x;
            meshletCount = meshletData.w; // Use mainMeshletCount (surface only, no skirts) to prevent shadow grid at tile boundaries
            baseVertexOffset = meshletData.z;
        }
    }

    // Broadcast tile data to all threads
    barrier();
    if (gl_LocalInvocationID.x == 0) {
        sharedTileData[0] = tileVisible ? 1 : 0;
        sharedTileData[1] = selectedLOD;
        sharedTileData[2] = meshletOffset;
        sharedTileData[3] = meshletCount;
        sharedTileData[4] = baseVertexOffset;
    }
    barrier();

    tileVisible = sharedTileData[0] != 0;
    selectedLOD = sharedTileData[1];
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

    // Meshlet-level frustum culling against light frustum
    uint meshletsPerThread = (meshletCount + TASK_WORKGROUP_SIZE - 1) / TASK_WORKGROUP_SIZE;

    for (uint i = 0; i < meshletsPerThread; i++) {
        uint localMeshletIndex = gl_LocalInvocationID.x + i * TASK_WORKGROUP_SIZE;

        if (localMeshletIndex < meshletCount) {
            uint globalMeshletIndex = meshletOffset + localMeshletIndex;
            GPUMeshlet meshlet = meshlets[globalMeshletIndex];

            vec4 worldSphere = transformBoundingSphere(meshlet.boundingSphere, tile.modelMatrix);
            bool meshletVisible = sphereInFrustum(worldSphere, sharedFrustumPlanes);

            if (meshletVisible) {
                uint slot = atomicAdd(sharedVisibleCount, 1);
                if (slot < MAX_MESHLETS_PER_PAYLOAD) {
                    sharedMeshletIndices[slot] = globalMeshletIndex;
                }
            }
        }
    }

    barrier();

    // Thread 0 builds payload and emits work
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

#type MESH
#version 460 core
#extension GL_EXT_mesh_shader : require
#extension GL_GOOGLE_include_directive : require

#include "../common/gpu_types.glsl"

const uint MESHLET_MAX_VERTICES = 64;
const uint MESHLET_MAX_PRIMITIVES = 124;
const uint MAX_MESHLETS_PER_PAYLOAD = 512;

layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;
layout(triangles, max_vertices = 64, max_primitives = 124) out;

layout(push_constant) uniform TerrainShadowPushConstants {
    mat4 lightViewProjection;
    uint tileCount;
    uint shadowLOD;
    float depthBias;
    float slopeBias;
    float normalBias;
    float _pad0;
    float _pad1;
    float _pad2;
} pc;

layout(std430, set = 0, binding = 0) readonly buffer TerrainTileBuffer {
    TerrainTileGPUData tiles[];
};

layout(std430, set = 1, binding = 0) readonly buffer MeshletBuffer {
    GPUMeshlet meshlets[];
};

layout(std430, set = 1, binding = 1) readonly buffer MeshletVertexBuffer {
    uint meshletVertices[];
};

layout(std430, set = 1, binding = 2) readonly buffer MeshletPrimitiveBuffer {
    uint meshletPrimitives[];
};

layout(std430, set = 2, binding = 0) readonly buffer VertexBuffer {
    float vertexData[];
};

struct TerrainShadowPayload {
    uint tileIndex;
    uint lodLevel;
    uint baseVertexOffset;
    uint meshletIndices[MAX_MESHLETS_PER_PAYLOAD];
    uint meshletCount;
};

taskPayloadSharedEXT TerrainShadowPayload payload;

shared vec3 sharedPositions[MESHLET_MAX_VERTICES];
shared vec3 sharedNormals[MESHLET_MAX_VERTICES];

uvec3 unpackPrimitive(uint packed) {
    return uvec3(
        packed & 0xFFu,
        (packed >> 8) & 0xFFu,
        (packed >> 16) & 0xFFu
    );
}

void main() {
    uint payloadMeshletIndex = gl_WorkGroupID.x;

    if (payloadMeshletIndex >= payload.meshletCount) {
        SetMeshOutputsEXT(0, 0);
        return;
    }

    uint globalMeshletIndex = payload.meshletIndices[payloadMeshletIndex];
    GPUMeshlet meshlet = meshlets[globalMeshletIndex];
    TerrainTileGPUData tile = tiles[payload.tileIndex];

    uint vertexCount, primitiveCount;
    unpackMeshletCounts(meshlet.vertexPrimCount, vertexCount, primitiveCount);
    SetMeshOutputsEXT(vertexCount, primitiveCount);

    mat4 modelMatrix = tile.modelMatrix;

    // First pass: Load vertex positions and normals into shared memory
    uint numIterations = (vertexCount + gl_WorkGroupSize.x - 1) / gl_WorkGroupSize.x;
    for (uint iter = 0; iter < numIterations; iter++) {
        uint localVertexIndex = iter * gl_WorkGroupSize.x + gl_LocalInvocationID.x;
        if (localVertexIndex < vertexCount) {
            uint meshletLocalVertexIdx = meshletVertices[meshlet.vertexOffset + localVertexIndex];
            uint globalVertexIndex = meshlet.globalVertexOffset + meshletLocalVertexIdx;
            uint baseIdx = globalVertexIndex * 16;

            vec3 position = vec3(
                vertexData[baseIdx + 0],
                vertexData[baseIdx + 1],
                vertexData[baseIdx + 2]
            );
            vec3 normal = vec3(
                vertexData[baseIdx + 3],
                vertexData[baseIdx + 4],
                vertexData[baseIdx + 5]
            );

            sharedPositions[localVertexIndex] = position;
            sharedNormals[localVertexIndex] = normal;
        }
    }

    barrier();

    // Second pass: Transform and output positions (depth-only)
    // Apply normal offset to push shadow surface behind rendered surface,
    // preventing self-shadowing when shadow LOD differs from render LOD
    for (uint iter = 0; iter < numIterations; iter++) {
        uint localVertexIndex = iter * gl_WorkGroupSize.x + gl_LocalInvocationID.x;
        if (localVertexIndex < vertexCount) {
            vec3 localPos = sharedPositions[localVertexIndex];
            vec3 localNormal = sharedNormals[localVertexIndex];
            localPos -= localNormal * pc.normalBias;
            vec4 worldPos = modelMatrix * vec4(localPos, 1.0);
            gl_MeshVerticesEXT[localVertexIndex].gl_Position = pc.lightViewProjection * worldPos;
        }
    }

    // Output primitive indices
    uint numPrimIterations = (primitiveCount + gl_WorkGroupSize.x - 1) / gl_WorkGroupSize.x;
    for (uint iter = 0; iter < numPrimIterations; iter++) {
        uint localPrimIndex = iter * gl_WorkGroupSize.x + gl_LocalInvocationID.x;
        if (localPrimIndex < primitiveCount) {
            uint packedPrimitive = meshletPrimitives[meshlet.primitiveOffset + localPrimIndex];
            uvec3 indices = unpackPrimitive(packedPrimitive);
            gl_PrimitiveTriangleIndicesEXT[localPrimIndex] = indices;
        }
    }
}
