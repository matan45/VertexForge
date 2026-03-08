#type MESH
#version 460 core
#extension GL_EXT_mesh_shader : require
#extension GL_GOOGLE_include_directive : require

#include "../common/gpu_types.glsl"
#include "../common/camera_types.glsl"
#include "wind_common.glsl"

const uint MESHLET_MAX_VERTICES = 64;
const uint MESHLET_MAX_PRIMITIVES = 124;

layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;
layout(triangles, max_vertices = 64, max_primitives = 124) out;

layout(location = 0) out vec3 fragWorldPos[];
layout(location = 1) out vec3 fragNormal[];
layout(location = 2) out vec2 fragTexCoord[];
layout(location = 3) flat out uint fragInstanceIndex[];

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

layout(set = 1, binding = 0) uniform CameraUBO {
    CameraData camera;
};

// Wind parameters
layout(set = 2, binding = 0) uniform WindUBO {
    vec4 windDirectionAndSpeed;  // xyz=direction, w=speed
    vec4 windGustParams;         // x=gustStrength, y=gustFrequency, z=turbulenceScale, w=time
};

// Meshlet data (shared with main mesh pipeline)
layout(std430, set = 3, binding = 0) readonly buffer MeshletBuffer {
    GPUMeshlet meshlets[];
};

layout(std430, set = 3, binding = 1) readonly buffer MeshletVertexBuffer {
    uint meshletVertices[];
};

layout(std430, set = 3, binding = 2) readonly buffer MeshletPrimitiveBuffer {
    uint meshletPrimitives[];
};

layout(std430, set = 4, binding = 0) readonly buffer VertexBuffer {
    float vertexData[];
};

struct VegetationPayload {
    uint instanceIndices[32];
    uint lodLevels[32];
    uint instanceCount;
};

taskPayloadSharedEXT VegetationPayload payload;

shared vec3 sharedPositions[MESHLET_MAX_VERTICES];
shared vec3 sharedNormals[MESHLET_MAX_VERTICES];
shared vec2 sharedTexCoords[MESHLET_MAX_VERTICES];

uvec3 unpackPrimitive(uint packed) {
    return uvec3(
        packed & 0xFFu,
        (packed >> 8) & 0xFFu,
        (packed >> 16) & 0xFFu
    );
}

void main() {
    uint payloadIndex = gl_WorkGroupID.x;
    if (payloadIndex >= payload.instanceCount) {
        SetMeshOutputsEXT(0, 0);
        return;
    }

    // Placeholder - actual implementation will:
    // 1. Load meshlet data from merged buffer using instance's mesh reference
    // 2. Transform vertices by instance modelMatrix
    // 3. Apply wind displacement using calculateWindDisplacement() from wind_common.glsl
    // 4. Emit vertices and primitives
    //
    // Wind application example (to be used when meshlet data is available):
    //   vec3 worldPos = (modelMatrix * vec4(localPos, 1.0)).xyz;
    //   float vertexHeight = (localPos.y - meshMinY) / (meshMaxY - meshMinY);
    //   vec3 windOffset = calculateWindDisplacement(worldPos, vertexHeight,
    //                                               windDirectionAndSpeed, windGustParams);
    //   worldPos += windOffset;

    SetMeshOutputsEXT(0, 0);
}
