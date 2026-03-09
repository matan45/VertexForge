#type TASK
#version 460 core
#extension GL_EXT_mesh_shader : require
#extension GL_GOOGLE_include_directive : require

layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;

struct VisibleInstance {
    uint instanceIndex;
    uint lodLevel;
};

layout(std430, set = 0, binding = 0) readonly buffer VisibleBuffer {
    VisibleInstance visibleInstances[];
};

layout(std430, set = 0, binding = 1) readonly buffer VisibleCountBuffer {
    uint visibleCount;
};

// Must match TreeInstanceGPU in VegetationGPUTypes.hpp
struct TreeInstance {
    mat4 modelMatrix;
    vec4 boundingSphere;
    uint speciesId;
    uint lodMask;
    float lodDistances[3];
    uint padding;
};

layout(std430, set = 0, binding = 2) readonly buffer TreeInstanceBuffer {
    TreeInstance allInstances[];
};

// Must match SpeciesRenderInfoGPU in VegetationGPUTypes.hpp
struct SpeciesRenderInfo {
    uint meshletOffset[4];  // per LOD
    uint meshletCount[4];   // per LOD
    uint baseVertexOffset;
    uint materialTextureIndex;
    uint padding[2];
};

layout(std430, set = 0, binding = 3) readonly buffer SpeciesRenderInfoBuffer {
    SpeciesRenderInfo speciesInfos[];
};

struct VegetationPayload {
    uint instanceIndex;
    uint baseMeshletIndex;  // global offset into meshlet buffer
    uint meshletCount;
    uint baseVertexOffset;
    uint materialTextureIndex;
};

taskPayloadSharedEXT VegetationPayload payload;

void main() {
    uint visIdx = gl_WorkGroupID.x;
    if (visIdx >= visibleCount) {
        EmitMeshTasksEXT(0, 1, 1);
        return;
    }

    VisibleInstance vis = visibleInstances[visIdx];
    TreeInstance inst = allInstances[vis.instanceIndex];
    SpeciesRenderInfo species = speciesInfos[inst.speciesId];

    uint lod = vis.lodLevel;
    uint meshletCount = species.meshletCount[lod];

    if (meshletCount == 0) {
        EmitMeshTasksEXT(0, 1, 1);
        return;
    }

    payload.instanceIndex = vis.instanceIndex;
    payload.baseMeshletIndex = species.meshletOffset[lod];
    payload.meshletCount = meshletCount;
    payload.baseVertexOffset = species.baseVertexOffset;
    payload.materialTextureIndex = species.materialTextureIndex;

    EmitMeshTasksEXT(meshletCount, 1, 1);
}
