#type COMPUTE
#version 450
#extension GL_GOOGLE_include_directive : require

#include "../common/camera_types.glsl"

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

// Must match TreeInstanceGPU in VegetationGPUTypes.hpp
struct TreeInstance {
    mat4 modelMatrix;
    vec4 boundingSphere;    // xyz=center, w=radius
    uint speciesId;
    uint lodMask;           // bits 0-2=mesh LODs
    float lodDistances[4];  // LOD0->1, LOD1->2, max render, (unused)
};

layout(std430, set = 0, binding = 0) readonly buffer TreeInstanceBuffer {
    TreeInstance instances[];
};

layout(std430, set = 0, binding = 1) readonly buffer InstanceCountBuffer {
    uint totalInstances;
};

// Output: visible instances sorted by rendering path
struct VisibleInstance {
    uint instanceIndex;
    uint lodLevel;
};

// Mesh LODs (LOD 0, 1, 2) - all go to mesh shader pipeline
layout(std430, set = 0, binding = 2) buffer VisibleMeshBuffer {
    VisibleInstance visibleMesh[];
};

layout(std430, set = 0, binding = 3) buffer CountersBuffer {
    uint meshCount;
};

// Camera - matches GPUCameraData
layout(set = 1, binding = 0) uniform CameraUBO {
    GPUCameraData camera;
};

bool sphereInFrustum(vec3 center, float radius) {
    for (int i = 0; i < 6; i++) {
        float distance = dot(camera.frustumPlanes[i].xyz, center) + camera.frustumPlanes[i].w;
        if (distance < -radius) {
            return false;
        }
    }
    return true;
}

void main() {
    uint idx = gl_GlobalInvocationID.x;
    if (idx >= totalInstances) return;

    TreeInstance inst = instances[idx];
    vec3 center = inst.boundingSphere.xyz;
    float radius = inst.boundingSphere.w;

    // Frustum cull
    if (camera.enableFrustumCulling != 0u && !sphereInFrustum(center, radius)) return;

    // Distance-based LOD selection
    float dist = distance(center, camera.cameraPosition.xyz);

    // Beyond max render distance
    if (camera.enableDistanceCulling != 0u && dist > inst.lodDistances[2]) return;

    uint lodLevel;
    if (dist < inst.lodDistances[0]) {
        lodLevel = 0;  // Mesh LOD0
    } else if (dist < inst.lodDistances[1]) {
        lodLevel = 1;  // Mesh LOD1
    } else {
        lodLevel = 2;  // Mesh LOD2
    }

    // Check if this LOD is available, fallback to nearest lower
    while (lodLevel > 0 && (inst.lodMask & (1u << lodLevel)) == 0u) {
        lodLevel--;
    }
    if ((inst.lodMask & (1u << lodLevel)) == 0u) return;

    // Write to mesh buffer
    VisibleInstance vis;
    vis.instanceIndex = idx;
    vis.lodLevel = lodLevel;

    uint outIdx = atomicAdd(meshCount, 1);
    visibleMesh[outIdx] = vis;
}
