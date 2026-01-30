#type COMPUTE
#version 450
#extension GL_GOOGLE_include_directive : require

// =========================================================================
// Prepare DAG Indirect Dispatch Command (VK-300)
//
// Simple compute shader that reads selectedCount from DAG traversal state
// and writes the indirect draw command for mesh shader dispatch.
// This enables proper indirect dispatch based on actual selection count
// instead of conservative maximum workgroups.
// =========================================================================

#include "../common/cluster_types.glsl"

layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;

// Traversal state buffer - contains selectedCount from DAG traversal
layout(std430, set = 0, binding = 0) readonly buffer TraversalStateBuffer {
    GPUDAGTraversalState state;
};

// Indirect draw command buffer
// Layout matches VkDrawMeshTasksIndirectCommandEXT:
// { uint groupCountX, uint groupCountY, uint groupCountZ }
layout(std430, set = 0, binding = 1) writeonly buffer IndirectCommandBuffer {
    uint groupCountX;
    uint groupCountY;
    uint groupCountZ;
} indirectCmd;

void main() {
    // Each task shader workgroup processes one cluster selection
    // So we dispatch selectedCount workgroups
    uint selectedCount = state.selectedCount;

    // Clamp to prevent excessive dispatch
    // MAX_CLUSTER_SELECTIONS_PER_FRAME = 1024 * 1024 = 1048576
    selectedCount = min(selectedCount, 1048576u);

    // Write indirect command
    indirectCmd.groupCountX = selectedCount;
    indirectCmd.groupCountY = 1u;
    indirectCmd.groupCountZ = 1u;
}
