#ifndef CLUSTER_CULLING_GLSL
#define CLUSTER_CULLING_GLSL

// Cluster-Forward Lighting Structures and Functions
// Shared between mesh_shader_gpudriven.glsl and mesh_terrain.glsl

//-----------------------------------------------------------------------------
// Constants
//-----------------------------------------------------------------------------

const uint LIGHT_INDEX_MASK = 0x7FFFFFFFu;

//-----------------------------------------------------------------------------
// Cluster Grid Data Structures
//-----------------------------------------------------------------------------

struct ClusterGridParams {
    uvec4 gridDimensions;   // xyz = tile counts, w = unused
    vec4 screenParams;      // xy = screen size, zw = tile size
    vec4 depthParams;       // x = near, y = far, z = unused, w = log scale factor
    mat4 invProjection;
    vec4 clusterScale;
    vec4 clusterBias;
};

struct ClusterLightData {
    uint offset;    // Offset into light index list
    uint counts;    // Packed: low 16 bits = point count, high 16 bits = spot count
};

//-----------------------------------------------------------------------------
// Cluster Helper Functions
//-----------------------------------------------------------------------------

float linearizeDepth(ClusterGridParams params, float windowZ) {
    float near = params.depthParams.x;
    float far = params.depthParams.y;
    float denominator = max(far - windowZ * (far - near), 0.0001);
    return near * far / denominator;
}

uint getClusterIndex(ClusterGridParams params, vec2 fragCoord, float viewZ) {
    float clampedZ = max(viewZ, params.depthParams.x);

    uint tileX = uint(fragCoord.x / params.screenParams.z);
    uint tileY = uint(fragCoord.y / params.screenParams.w);

    float logRatio = log(clampedZ / params.depthParams.x);
    uint slice = uint(logRatio * params.depthParams.w);

    tileX = min(tileX, params.gridDimensions.x - 1u);
    tileY = min(tileY, params.gridDimensions.y - 1u);
    slice = min(slice, params.gridDimensions.z - 1u);

    return tileX + tileY * params.gridDimensions.x +
           slice * params.gridDimensions.x * params.gridDimensions.y;
}

uint getClusterPointLightCount(ClusterLightData clusterData) {
    return clusterData.counts & 0xFFFFu;
}

uint getClusterSpotLightCount(ClusterLightData clusterData) {
    return clusterData.counts >> 16u;
}

uint extractLightIndex(uint packedIndex) {
    return packedIndex & LIGHT_INDEX_MASK;
}

#endif // CLUSTER_CULLING_GLSL
