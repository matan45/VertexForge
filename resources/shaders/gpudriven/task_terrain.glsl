#type TASK
#version 460 core
#extension GL_EXT_mesh_shader : require
#extension GL_GOOGLE_include_directive : require

#include "../common/gpu_types.glsl"
#include "../common/camera_types.glsl"
#include "../common/culling_functions.glsl"
#include "../common/hiz_occlusion.glsl"

layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;

const uint TASK_WORKGROUP_SIZE = 32;
const uint MAX_MESHLETS_PER_PAYLOAD = 512; // For High (129x129) tiles

layout(set = 0, binding = 0) uniform CameraUBO {
    CameraData camera;
};

// Set 11 - terrain-specific data
layout(std430, set = 11, binding = 0) readonly buffer TerrainTileBuffer {
    TerrainTileGPUData tiles[];
};

layout(std430, set = 3, binding = 0) readonly buffer MeshletBuffer {
    GPUMeshlet meshlets[];
};

layout(std430, set = 11, binding = 1) buffer TerrainStatsBuffer {
    uint totalTiles;
    uint culledTiles;
    uint totalMeshlets;
    uint culledMeshlets;
    uint visibleMeshlets;
    uint lodCount0;
    uint lodCount1;
    uint lodCount2;
    uint lodCount3;
    uint lodCount4;
    uint lodCount5;
    uint culledByOcclusion;
    uint padding[3];
} stats;

// Hi-Z texture for meshlet occlusion culling (from depth prepass)
layout(set = 3, binding = 4) uniform sampler2D meshletHiZTexture;

layout(push_constant) uniform PushConstants {
    uint tileCount;
    uint viewMode;
    float screenWidth;
    float screenHeight;
    float lodBias;          // LOD quality bias (1.0 = normal)
    float errorThreshold;   // Screen-space error threshold in pixels
    float terrainTextureScale;     // Scale for world-space UV tiling
    float terrainMaxDrawDistSq;    // Squared max draw distance for terrain (0 = disabled)
    vec2 brushWorldPos;            // Brush overlay world position
    float brushWorldRadius;        // 0.0 = inactive
    float brushFalloff;
    float brushShape;
    float shadowLOD;
    uint hiZMipLevels;             // Mip levels in the Hi-Z pyramid (0 = disabled)
    float _pad3;                   // Align mat4 to 16-byte boundary
    mat4 viewProjection;
} pc;

const uint TERRAIN_CULL_FRUSTUM_BIT = 0x100u;
const uint TERRAIN_CULL_BACKFACE_BIT = 0x200u;
const uint TERRAIN_DEBUG_FORCE_LOD0_BIT = 0x400u;
const uint TERRAIN_CULL_OCCLUSION_BIT = 0x800u;

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
shared uint sharedTileData[8];

// Returns the coarsest LOD level where screen-space error is still acceptable
uint selectLODByGeometricError(TerrainTileGPUData tile, float distance, float screenHeight) {
    if (distance < 0.01) {
        return 0; // Closest LOD (highest detail)
    }

    float lodBias = pc.lodBias > 0.0 ? pc.lodBias : 1.0;
    float threshold = pc.errorThreshold > 0.0 ? pc.errorThreshold : 2.0; // Default 2 pixels

    // Calculate screen-space error factor
    // error_screen = error_world * screenHeight / distance
    // Higher lodBias = stricter quality (prefer finer LODs)
    float screenFactor = screenHeight / distance * lodBias;

    // Select the COARSEST LOD where projected error is below threshold
    // Check from coarsest (LOD 5) to finest (LOD 0)
    if (tile.lodGeometricErrors2.y * screenFactor < threshold) return 5;
    if (tile.lodGeometricErrors2.x * screenFactor < threshold) return 4;
    if (tile.lodGeometricErrors.w * screenFactor < threshold) return 3;
    if (tile.lodGeometricErrors.z * screenFactor < threshold) return 2;
    if (tile.lodGeometricErrors.y * screenFactor < threshold) return 1;
    return 0; // Default to highest detail
}

// Tries the ideal LOD first, then searches for alternatives
uint findBestAvailableLOD(TerrainTileGPUData tile, uint idealLOD) {
    uvec4 data = getTerrainLODMeshletData(tile, idealLOD);
    if (data.y > 0) return idealLOD; // meshletCount > 0

    // Try finer LODs first (lower indices = higher detail)
    for (uint lod = 0; lod < 6; lod++) {
        uvec4 lodData = getTerrainLODMeshletData(tile, lod);
        if (lodData.y > 0) return lod;
    }

    // Nothing available
    return idealLOD; // Return ideal even if empty (will result in 0 meshlets)
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

    // Only thread 0 does tile-level work
    uint selectedLOD = 0;
    uint meshletOffset = 0;
    uint meshletCount = 0;
    uint baseVertexOffset = 0;
    bool tileVisible = true;

    if (gl_LocalInvocationID.x == 0) {
        atomicAdd(stats.totalTiles, 1);

        // Stage 1: Tile-level frustum culling using AABB
        if ((pc.viewMode & TERRAIN_CULL_FRUSTUM_BIT) != 0u) {
            tileVisible = aabbInFrustum(tile.aabbMin.xyz, tile.aabbMax.xyz, camera.frustumPlanes);
            if (!tileVisible) {
                atomicAdd(stats.culledTiles, 1);
            }
        }

        // Stage 1b: Distance culling for terrain tiles
        if (tileVisible && pc.terrainMaxDrawDistSq > 0.0) {
            vec3 diff = tile.boundingSphere.xyz - camera.cameraPos;
            float distSq = dot(diff, diff);
            if (distSq > pc.terrainMaxDrawDistSq) {
                tileVisible = false;
                atomicAdd(stats.culledTiles, 1);
            }
        }

        // Stage 1c: Tile-level Hi-Z occlusion culling (AABB test)
        if (tileVisible && (pc.viewMode & TERRAIN_CULL_OCCLUSION_BIT) != 0u && pc.hiZMipLevels > 0u) {
            mat4 vp = camera.projection * camera.view;
            if (!hiZOcclusionTestAABB(meshletHiZTexture, tile.aabbMin.xyz, tile.aabbMax.xyz,
                                       vp, vec2(pc.screenWidth, pc.screenHeight), pc.hiZMipLevels)) {
                tileVisible = false;
                atomicAdd(stats.culledTiles, 1);
                atomicAdd(stats.culledByOcclusion, 1);
            }
        }

        if (tileVisible) {
            // Stage 2: GPU LOD selection based on geometric error
            float distance = length(tile.boundingSphere.xyz - camera.cameraPos);
            uint idealLOD = ((pc.viewMode & TERRAIN_DEBUG_FORCE_LOD0_BIT) != 0u)
                ? 0u
                : selectLODByGeometricError(tile, distance, pc.screenHeight);

            // Find the best available LOD (handles streaming where only one LOD is loaded)
            selectedLOD = findBestAvailableLOD(tile, idealLOD);

            if (selectedLOD == 0) atomicAdd(stats.lodCount0, 1);
            else if (selectedLOD == 1) atomicAdd(stats.lodCount1, 1);
            else if (selectedLOD == 2) atomicAdd(stats.lodCount2, 1);
            else if (selectedLOD == 3) atomicAdd(stats.lodCount3, 1);
            else if (selectedLOD == 4) atomicAdd(stats.lodCount4, 1);
            else atomicAdd(stats.lodCount5, 1);

            uvec4 meshletData = getTerrainLODMeshletData(tile, selectedLOD);
            meshletOffset = meshletData.x;
            meshletCount = meshletData.y;
            baseVertexOffset = meshletData.z;
        }
    }

    // Broadcast tile visibility and LOD data to all threads
    barrier();
    if (gl_LocalInvocationID.x == 0) {
        sharedTileData[0] = tileVisible ? 1 : 0;
        sharedTileData[1] = selectedLOD;
        sharedTileData[2] = meshletOffset;
        sharedTileData[3] = meshletCount;
        sharedTileData[4] = baseVertexOffset;
    }
    barrier();

    // All threads read the broadcast data
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

    // Stage 3: Meshlet-level culling within the tile (surface + cave)
    // Combine surface and cave meshlet counts for unified processing
    uint caveMeshletOffset = tile.caveMeshletData.x;
    uint caveMeshletCount = tile.caveMeshletData.y;
    uint totalMeshletCount = meshletCount + caveMeshletCount;

    uint meshletsPerThread = (totalMeshletCount + TASK_WORKGROUP_SIZE - 1) / TASK_WORKGROUP_SIZE;

    for (uint i = 0; i < meshletsPerThread; i++) {
        uint localMeshletIndex = gl_LocalInvocationID.x + i * TASK_WORKGROUP_SIZE;

        if (localMeshletIndex < totalMeshletCount) {
            // Determine if this is a surface meshlet or a cave meshlet
            uint globalMeshletIndex;
            if (localMeshletIndex < meshletCount) {
                globalMeshletIndex = meshletOffset + localMeshletIndex;
            } else {
                globalMeshletIndex = caveMeshletOffset + (localMeshletIndex - meshletCount);
            }

            GPUMeshlet meshlet = meshlets[globalMeshletIndex];

            atomicAdd(stats.totalMeshlets, 1);

            // For terrain, model matrix is usually identity, but support transforms
            vec4 worldSphere = transformBoundingSphere(meshlet.boundingSphere, tile.modelMatrix);

            bool meshletVisible = true;

            if ((pc.viewMode & TERRAIN_CULL_FRUSTUM_BIT) != 0u) {
                if (!sphereInFrustum(worldSphere, camera.frustumPlanes)) {
                    meshletVisible = false;
                    atomicAdd(stats.culledMeshlets, 1);
                }
            }

            // Backface culling for meshlet (optional for terrain as most faces are visible)
            if (meshletVisible && (pc.viewMode & TERRAIN_CULL_BACKFACE_BIT) != 0u) {
                if (!coneCullTest(meshlet.cone, tile.modelMatrix, camera.cameraPos, worldSphere.xyz)) {
                    meshletVisible = false;
                    atomicAdd(stats.culledMeshlets, 1);
                }
            }

            // Meshlet-level Hi-Z occlusion culling
            if (meshletVisible && (pc.viewMode & TERRAIN_CULL_OCCLUSION_BIT) != 0u && pc.hiZMipLevels > 0u) {
                mat4 vp = camera.projection * camera.view;
                if (!hiZOcclusionTest(meshletHiZTexture, worldSphere, vp,
                                      vec2(pc.screenWidth, pc.screenHeight), pc.hiZMipLevels)) {
                    meshletVisible = false;
                    atomicAdd(stats.culledMeshlets, 1);
                    atomicAdd(stats.culledByOcclusion, 1);
                }
            }

            if (meshletVisible) {
                atomicAdd(stats.visibleMeshlets, 1);
                uint slot = atomicAdd(sharedVisibleCount, 1);
                if (slot < MAX_MESHLETS_PER_PAYLOAD) {
                    sharedMeshletIndices[slot] = globalMeshletIndex;
                }
            }
        }
    }

    barrier();

    // Thread 0 builds the payload and emits work
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
