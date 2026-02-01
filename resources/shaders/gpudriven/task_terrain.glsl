#type TASK
#version 460 core
#extension GL_EXT_mesh_shader : require
#extension GL_GOOGLE_include_directive : require

#include "../common/gpu_types.glsl"
#include "../common/camera_types.glsl"

layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;

const uint TASK_WORKGROUP_SIZE = 32;
const uint MAX_MESHLETS_PER_PAYLOAD = 512; // For High (129x129) tiles

// Camera data
layout(set = 0, binding = 0) uniform CameraUBO {
    CameraData camera;
};

// Terrain tile data
layout(std430, set = 6, binding = 0) readonly buffer TerrainTileBuffer {
    TerrainTileGPUData tiles[];
};

// Meshlet data
layout(std430, set = 3, binding = 0) readonly buffer MeshletBuffer {
    GPUMeshlet meshlets[];
};

// Terrain culling statistics
layout(std430, set = 6, binding = 1) buffer TerrainStatsBuffer {
    uint totalTiles;
    uint culledTiles;
    uint totalMeshlets;
    uint culledMeshlets;
    uint visibleMeshlets;
    uint lodCount0;
    uint lodCount1;
    uint lodCount2;
    uint lodCount3;
    uint padding[3];
} stats;

layout(push_constant) uniform PushConstants {
    uint tileCount;
    uint viewMode;
    float screenWidth;
    float screenHeight;
    float lodBias;          // LOD quality bias (1.0 = normal)
    float errorThreshold;   // Screen-space error threshold in pixels
} pc;

const uint TERRAIN_CULL_FRUSTUM_BIT = 0x100u;
const uint TERRAIN_CULL_BACKFACE_BIT = 0x200u;

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
shared uint sharedTileData[8]; // For broadcasting tile visibility/LOD data

// Test if AABB is inside frustum
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

// Test if sphere is inside frustum
bool sphereInFrustum(vec4 sphere, vec4 frustumPlanes[6]) {
    for (int i = 0; i < 6; i++) {
        float distance = dot(frustumPlanes[i].xyz, sphere.xyz) + frustumPlanes[i].w;
        if (distance < -sphere.w) {
            return false;
        }
    }
    return true;
}

// Transform bounding sphere by model matrix
vec4 transformBoundingSphere(vec4 localSphere, mat4 modelMatrix) {
    vec3 worldCenter = (modelMatrix * vec4(localSphere.xyz, 1.0)).xyz;
    float scaleX = length(modelMatrix[0].xyz);
    float scaleY = length(modelMatrix[1].xyz);
    float scaleZ = length(modelMatrix[2].xyz);
    float maxScale = max(max(scaleX, scaleY), scaleZ);
    float worldRadius = localSphere.w * maxScale;
    return vec4(worldCenter, worldRadius);
}

// Backface cone culling test
bool coneCullTest(vec4 cone, mat4 modelMatrix, vec3 cameraPos, vec3 meshletCenter) {
    if (cone.w >= 1.0) {
        return true; // No valid cone, pass the test
    }
    vec3 worldConeAxis = normalize(mat3(modelMatrix) * cone.xyz);
    vec3 viewDir = normalize(meshletCenter - cameraPos);
    float dotProduct = dot(viewDir, worldConeAxis);
    return dotProduct < cone.w;
}

// Select LOD based on screen-space geometric error
// Returns the coarsest LOD level where error is still acceptable
uint selectLODByGeometricError(TerrainTileGPUData tile, float distance, float screenHeight) {
    // Avoid division by zero
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
    // Check from coarsest (LOD 3) to finest (LOD 0)
    // LOD 3 has the highest geometric error
    if (tile.lodGeometricErrors.w * screenFactor < threshold) return 3;
    if (tile.lodGeometricErrors.z * screenFactor < threshold) return 2;
    if (tile.lodGeometricErrors.y * screenFactor < threshold) return 1;
    return 0; // Default to highest detail
}

// Find the best available LOD (one that has meshlets loaded)
// Tries the ideal LOD first, then searches for alternatives
uint findBestAvailableLOD(TerrainTileGPUData tile, uint idealLOD) {
    // First try the ideal LOD
    uvec4 data = getTerrainLODMeshletData(tile, idealLOD);
    if (data.y > 0) return idealLOD; // meshletCount > 0

    // Try finer LODs first (lower indices = higher detail)
    for (uint lod = 0; lod < 4; lod++) {
        uvec4 lodData = getTerrainLODMeshletData(tile, lod);
        if (lodData.y > 0) return lod;
    }

    // Nothing available
    return idealLOD; // Return ideal even if empty (will result in 0 meshlets)
}

void main() {
    uint tileIndex = gl_WorkGroupID.x;

    // Initialize shared memory
    if (gl_LocalInvocationID.x == 0) {
        sharedVisibleCount = 0;
    }
    barrier();

    // Check if tile index is valid
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

        if (tileVisible) {
            // Stage 2: GPU LOD selection based on geometric error
            float distance = length(tile.boundingSphere.xyz - camera.cameraPos);
            uint idealLOD = selectLODByGeometricError(tile, distance, pc.screenHeight);

            // Find the best available LOD (handles streaming where only one LOD is loaded)
            selectedLOD = findBestAvailableLOD(tile, idealLOD);

            // Track LOD distribution
            if (selectedLOD == 0) atomicAdd(stats.lodCount0, 1);
            else if (selectedLOD == 1) atomicAdd(stats.lodCount1, 1);
            else if (selectedLOD == 2) atomicAdd(stats.lodCount2, 1);
            else atomicAdd(stats.lodCount3, 1);

            // Get meshlet data for selected LOD
            uvec4 meshletData = getTerrainLODMeshletData(tile, selectedLOD);
            meshletOffset = meshletData.x;
            meshletCount = meshletData.y;
            baseVertexOffset = meshletData.z;
        }
    }

    // Broadcast tile visibility and LOD data to all threads
    barrier();
    // Use dedicated shared memory for tile data broadcast
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

    // Reset shared visible count
    if (gl_LocalInvocationID.x == 0) {
        sharedVisibleCount = 0;
    }
    barrier();

    // Early exit if tile is culled or has no meshlets
    if (!tileVisible || meshletCount == 0) {
        if (gl_LocalInvocationID.x == 0) {
            payload.meshletCount = 0;
            EmitMeshTasksEXT(0, 1, 1);
        }
        return;
    }

    // Stage 3: Meshlet-level culling within the tile
    // Each thread processes multiple meshlets if needed
    uint meshletsPerThread = (meshletCount + TASK_WORKGROUP_SIZE - 1) / TASK_WORKGROUP_SIZE;

    for (uint i = 0; i < meshletsPerThread; i++) {
        uint localMeshletIndex = gl_LocalInvocationID.x + i * TASK_WORKGROUP_SIZE;

        if (localMeshletIndex < meshletCount) {
            uint globalMeshletIndex = meshletOffset + localMeshletIndex;
            GPUMeshlet meshlet = meshlets[globalMeshletIndex];

            atomicAdd(stats.totalMeshlets, 1);

            // For terrain, model matrix is usually identity, but support transforms
            vec4 worldSphere = transformBoundingSphere(meshlet.boundingSphere, tile.modelMatrix);

            bool meshletVisible = true;

            // Frustum culling for meshlet
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
