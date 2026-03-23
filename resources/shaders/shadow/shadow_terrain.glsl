#type TASK
#version 460 core
#extension GL_EXT_mesh_shader : require
#extension GL_GOOGLE_include_directive : require

#include "../common/gpu_types.glsl"
#include "../common/culling_functions.glsl"

layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;

const uint TASK_WORKGROUP_SIZE = 32;
const uint MAX_MESHLETS_PER_PAYLOAD = 512;

layout(push_constant) uniform TerrainShadowPushConstants {
    mat4 lightViewProjection;
    uint tileCount;
    uint shadowLOD;
    float depthBias;
    float slopeBias;
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
        extractFrustumPlanesFromVP(pc.lightViewProjection, sharedFrustumPlanes);
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

    // First pass: Load vertex positions into shared memory
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

            sharedPositions[localVertexIndex] = position;
        }
    }

    barrier();

    // Second pass: Transform and output positions (depth-only)
    for (uint iter = 0; iter < numIterations; iter++) {
        uint localVertexIndex = iter * gl_WorkGroupSize.x + gl_LocalInvocationID.x;
        if (localVertexIndex < vertexCount) {
            vec4 worldPos = modelMatrix * vec4(sharedPositions[localVertexIndex], 1.0);
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
