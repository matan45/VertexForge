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
    uint lodMask;           // Available LOD levels bitmask
    float lodDistances[3];  // LOD0->1, LOD1->2, max render
    uint padding;
};

layout(std430, set = 0, binding = 0) readonly buffer TreeInstanceBuffer {
    TreeInstance instances[];
};

layout(std430, set = 0, binding = 1) readonly buffer InstanceCountBuffer {
    uint totalInstances;
};

// Output: visible instances sorted by LOD
struct VisibleInstance {
    uint instanceIndex;
    uint lodLevel;
};

layout(std430, set = 0, binding = 2) buffer VisibleLOD0Buffer {
    VisibleInstance visibleLOD0[];
};

layout(std430, set = 0, binding = 3) buffer VisibleLOD1Buffer {
    VisibleInstance visibleLOD1[];
};

layout(std430, set = 0, binding = 4) buffer VisibleLOD2Buffer {
    VisibleInstance visibleLOD2[];  // Imposters
};

layout(std430, set = 0, binding = 5) buffer CountersBuffer {
    uint lod0Count;
    uint lod1Count;
    uint lod2Count;
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

    // Frustum cull using pre-extracted planes from camera
    if (camera.enableFrustumCulling != 0u && !sphereInFrustum(center, radius)) return;

    // Distance-based LOD selection
    float dist = distance(center, camera.cameraPosition.xyz);

    // Beyond max render distance
    if (camera.enableDistanceCulling != 0u && dist > inst.lodDistances[2]) return;

    uint lodLevel;
    if (dist < inst.lodDistances[0]) {
        lodLevel = 0;
    } else if (dist < inst.lodDistances[1]) {
        lodLevel = 1;
    } else {
        lodLevel = 2;
    }

    // Check if this LOD is available
    if ((inst.lodMask & (1u << lodLevel)) == 0u) {
        // Fallback to nearest available lower LOD
        if (lodLevel > 0 && (inst.lodMask & (1u << (lodLevel - 1))) != 0u) {
            lodLevel = lodLevel - 1;
        } else {
            return; // No suitable LOD
        }
    }

    // Write to appropriate LOD buffer
    VisibleInstance vis;
    vis.instanceIndex = idx;
    vis.lodLevel = lodLevel;

    if (lodLevel == 0) {
        uint outIdx = atomicAdd(lod0Count, 1);
        visibleLOD0[outIdx] = vis;
    } else if (lodLevel == 1) {
        uint outIdx = atomicAdd(lod1Count, 1);
        visibleLOD1[outIdx] = vis;
    } else {
        uint outIdx = atomicAdd(lod2Count, 1);
        visibleLOD2[outIdx] = vis;
    }
}
