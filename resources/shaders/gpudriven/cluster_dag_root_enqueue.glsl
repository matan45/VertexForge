#type COMPUTE
#version 450
#extension GL_GOOGLE_include_directive : require

// =========================================================================
// Cluster DAG Root Enqueue Compute Shader (VK-291)
//
// Initializes the work queue with root clusters for all DAG objects.
// One thread per object - enqueues root cluster index if object uses DAG.
// =========================================================================

#include "../common/gpu_types.glsl"
#include "../common/camera_types.glsl"
#include "../common/cluster_types.glsl"

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

// =========================================================================
// Buffer Bindings
// =========================================================================

layout(std430, set = 0, binding = 0) readonly buffer ObjectBuffer {
    GPUObjectData objects[];
};

layout(set = 0, binding = 1) uniform CameraUBO {
    GPUCameraData camera;
};

layout(std430, set = 0, binding = 9) readonly buffer TraversalParamsBuffer {
    GPUClusterTraversalParams params;
};

layout(std430, set = 0, binding = 10) buffer TraversalStateBuffer {
    GPUDAGTraversalState state;
};

layout(std430, set = 0, binding = 11) buffer WorkQueueA {
    uint workQueueA[];
};

// =========================================================================
// Helper Functions
// =========================================================================

// Pack object index and local cluster index into single uint
// Format: (objectIndex << 20) | (localClusterIndex & 0xFFFFF)
uint packWorkItem(uint objectIndex, uint localClusterIndex) {
    return (objectIndex << 20) | (localClusterIndex & 0xFFFFFu);
}

// =========================================================================
// Main Entry Point
// =========================================================================

void main() {
    uint objIdx = gl_GlobalInvocationID.x;

    // Early exit if beyond object count
    if (objIdx >= camera.objectCount) {
        return;
    }

    // Load object data
    GPUObjectData obj = objects[objIdx];

    // Skip non-DAG objects
    if (!usesClusterDAG(obj)) {
        return;
    }

    // Skip if DAG not fully loaded (streaming not complete)
    if (!isDAGFullyLoaded(obj)) {
        return;
    }

    // Get root cluster index for this object's DAG
    uint rootClusterIdx = getDagRootClusterIndex(obj);

    // Allocate slot in work queue (atomic)
    uint queueIdx = atomicAdd(state.inputQueueCount, 1u);

    // Bounds check to prevent buffer overflow
    if (queueIdx >= params.maxWorkQueueEntries) {
        return;
    }

    // Pack and store work item
    uint packed = packWorkItem(objIdx, rootClusterIdx);
    workQueueA[queueIdx] = packed;
}
