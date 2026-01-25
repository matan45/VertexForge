#type TASK
#version 460 core
#extension GL_EXT_mesh_shader : require

layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;

const uint TASK_WORKGROUP_SIZE = 32;
const uint MAX_MESHLETS_PER_PAYLOAD = 32;

// Must match PerDrawData in GPUDrivenTypes.hpp (240 bytes)
struct PerDrawData {
    mat4 modelMatrix;
    mat4 normalMatrix;

    vec4 albedo;
    vec4 materialParams;

    uvec4 textureIndices0;
    uvec4 textureIndices1;

    uint objectIndex;
    uint flags;
    float iblDiffuse;
    float iblSpecular;

    uint lodLevel;
    uint shaderGroupIndex;
    uint meshletOffset;
    uint meshletCount;

    uint baseVertexOffset;
    uint boneMatrixOffset;
    uint boneCount;
    uint padding3;
};

// Must match GPUMeshlet in MeshletBufferTypes.hpp (48 bytes)
struct GPUMeshlet {
    uint vertexOffset;
    uint primitiveOffset;
    uint vertexPrimCount;
    uint globalVertexOffset;
    vec4 boundingSphere;
    vec4 cone;
};

// Shadow push constants
layout(push_constant) uniform ShadowPushConstants {
    mat4 lightViewProjection;
    uint baseDrawIndex;
    float depthBias;
    float slopeBias;
    float normalBias;
} pc;

layout(std430, set = 0, binding = 0) readonly buffer PerDrawDataBuffer {
    PerDrawData perDrawData[];
};

layout(std430, set = 1, binding = 0) readonly buffer MeshletBuffer {
    GPUMeshlet meshlets[];
};

struct MeshletPayload {
    uint drawIndex;
    uint meshletIndices[MAX_MESHLETS_PER_PAYLOAD];
    uint meshletCount;
};

taskPayloadSharedEXT MeshletPayload payload;

shared uint sharedVisibleCount;
shared uint sharedMeshletIndices[TASK_WORKGROUP_SIZE];
shared vec4 sharedFrustumPlanes[6];  // Extracted once, shared across workgroup

vec4 transformBoundingSphere(vec4 localSphere, mat4 modelMatrix) {
    vec3 worldCenter = (modelMatrix * vec4(localSphere.xyz, 1.0)).xyz;
    float scaleX = length(modelMatrix[0].xyz);
    float scaleY = length(modelMatrix[1].xyz);
    float scaleZ = length(modelMatrix[2].xyz);
    float maxScale = max(max(scaleX, scaleY), scaleZ);
    float worldRadius = localSphere.w * maxScale;
    return vec4(worldCenter, worldRadius);
}

// Extract frustum planes from view-projection matrix
void extractFrustumPlanes(mat4 vp, out vec4 planes[6]) {
    // Left
    planes[0] = vec4(
        vp[0][3] + vp[0][0],
        vp[1][3] + vp[1][0],
        vp[2][3] + vp[2][0],
        vp[3][3] + vp[3][0]
    );
    // Right
    planes[1] = vec4(
        vp[0][3] - vp[0][0],
        vp[1][3] - vp[1][0],
        vp[2][3] - vp[2][0],
        vp[3][3] - vp[3][0]
    );
    // Bottom
    planes[2] = vec4(
        vp[0][3] + vp[0][1],
        vp[1][3] + vp[1][1],
        vp[2][3] + vp[2][1],
        vp[3][3] + vp[3][1]
    );
    // Top
    planes[3] = vec4(
        vp[0][3] - vp[0][1],
        vp[1][3] - vp[1][1],
        vp[2][3] - vp[2][1],
        vp[3][3] - vp[3][1]
    );
    // Near
    planes[4] = vec4(
        vp[0][3] + vp[0][2],
        vp[1][3] + vp[1][2],
        vp[2][3] + vp[2][2],
        vp[3][3] + vp[3][2]
    );
    // Far
    planes[5] = vec4(
        vp[0][3] - vp[0][2],
        vp[1][3] - vp[1][2],
        vp[2][3] - vp[2][2],
        vp[3][3] - vp[3][2]
    );

    // Normalize planes (with epsilon to prevent division by zero)
    const float PLANE_NORMALIZE_EPSILON = 0.0001;
    for (int i = 0; i < 6; i++) {
        float len = max(length(planes[i].xyz), PLANE_NORMALIZE_EPSILON);
        planes[i] /= len;
    }
}

bool sphereInFrustum(vec4 sphere, vec4 frustumPlanes[6]) {
    for (int i = 0; i < 6; i++) {
        float distance = dot(frustumPlanes[i].xyz, sphere.xyz) + frustumPlanes[i].w;
        if (distance < -sphere.w) {
            return false;
        }
    }
    return true;
}

void main() {
    uint drawIndex = pc.baseDrawIndex + gl_DrawID;
    PerDrawData drawData = perDrawData[drawIndex];

    uint localMeshletIndex = gl_LocalInvocationID.x;
    uint workgroupMeshletBase = gl_WorkGroupID.x * TASK_WORKGROUP_SIZE;
    uint meshletIndex = workgroupMeshletBase + localMeshletIndex;

    // Extract frustum planes once in invocation 0, share via shared memory
    if (gl_LocalInvocationID.x == 0) {
        sharedVisibleCount = 0;

        // Extract and normalize frustum planes from light view-projection matrix
        mat4 vp = pc.lightViewProjection;
        sharedFrustumPlanes[0] = vec4(vp[0][3] + vp[0][0], vp[1][3] + vp[1][0], vp[2][3] + vp[2][0], vp[3][3] + vp[3][0]); // Left
        sharedFrustumPlanes[1] = vec4(vp[0][3] - vp[0][0], vp[1][3] - vp[1][0], vp[2][3] - vp[2][0], vp[3][3] - vp[3][0]); // Right
        sharedFrustumPlanes[2] = vec4(vp[0][3] + vp[0][1], vp[1][3] + vp[1][1], vp[2][3] + vp[2][1], vp[3][3] + vp[3][1]); // Bottom
        sharedFrustumPlanes[3] = vec4(vp[0][3] - vp[0][1], vp[1][3] - vp[1][1], vp[2][3] - vp[2][1], vp[3][3] - vp[3][1]); // Top
        sharedFrustumPlanes[4] = vec4(vp[0][3] + vp[0][2], vp[1][3] + vp[1][2], vp[2][3] + vp[2][2], vp[3][3] + vp[3][2]); // Near
        sharedFrustumPlanes[5] = vec4(vp[0][3] - vp[0][2], vp[1][3] - vp[1][2], vp[2][3] - vp[2][2], vp[3][3] - vp[3][2]); // Far

        // Normalize planes (with epsilon to prevent division by zero for degenerate matrices)
        const float PLANE_NORMALIZE_EPSILON = 0.0001;
        for (int i = 0; i < 6; i++) {
            float len = max(length(sharedFrustumPlanes[i].xyz), PLANE_NORMALIZE_EPSILON);
            sharedFrustumPlanes[i] /= len;
        }
    }
    barrier();

    bool isValidMeshlet = meshletIndex < drawData.meshletCount;
    bool isVisible = false;

    if (isValidMeshlet) {
        uint globalMeshletIndex = drawData.meshletOffset + meshletIndex;
        GPUMeshlet meshlet = meshlets[globalMeshletIndex];
        vec4 worldSphere = transformBoundingSphere(meshlet.boundingSphere, drawData.modelMatrix);

        // Frustum culling using shared planes (extracted once by invocation 0)
        isVisible = sphereInFrustum(worldSphere, sharedFrustumPlanes);

        if (isVisible) {
            uint slot = atomicAdd(sharedVisibleCount, 1);
            if (slot < TASK_WORKGROUP_SIZE) {
                sharedMeshletIndices[slot] = globalMeshletIndex;
            }
        }
    }

    barrier();

    if (gl_LocalInvocationID.x == 0) {
        uint visibleCount = min(sharedVisibleCount, MAX_MESHLETS_PER_PAYLOAD);
        payload.drawIndex = drawIndex;
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

const uint MESHLET_MAX_VERTICES = 64;
const uint MESHLET_MAX_PRIMITIVES = 124;

layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;
layout(triangles, max_vertices = 64, max_primitives = 124) out;

// No fragment outputs needed for depth-only pass

// Must match PerDrawData in GPUDrivenTypes.hpp (240 bytes)
struct PerDrawData {
    mat4 modelMatrix;
    mat4 normalMatrix;

    vec4 albedo;
    vec4 materialParams;

    uvec4 textureIndices0;
    uvec4 textureIndices1;

    uint objectIndex;
    uint flags;
    float iblDiffuse;
    float iblSpecular;

    uint lodLevel;
    uint shaderGroupIndex;
    uint meshletOffset;
    uint meshletCount;

    uint baseVertexOffset;
    uint boneMatrixOffset;
    uint boneCount;
    uint padding3;
};

// Must match GPUMeshlet in MeshletBufferTypes.hpp (48 bytes)
struct GPUMeshlet {
    uint vertexOffset;
    uint primitiveOffset;
    uint vertexPrimCount;
    uint globalVertexOffset;
    vec4 boundingSphere;
    vec4 cone;
};

// Shadow push constants
layout(push_constant) uniform ShadowPushConstants {
    mat4 lightViewProjection;
    uint baseDrawIndex;
    float depthBias;
    float slopeBias;
    float normalBias;
} pc;

layout(std430, set = 0, binding = 0) readonly buffer PerDrawDataBuffer {
    PerDrawData perDrawData[];
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

// Raw float array: 16 floats per vertex (position.xyz, normal.xyz, texCoord.xy, boneIndices.xyzw, boneWeights.xyzw)
layout(std430, set = 2, binding = 0) readonly buffer VertexBuffer {
    float vertexData[];
};

layout(std430, set = 3, binding = 0) readonly buffer BoneMatrices {
    mat4 boneMatrices[];
};

const uint MAX_MESHLETS_PER_PAYLOAD = 32;

struct MeshletPayload {
    uint drawIndex;
    uint meshletIndices[MAX_MESHLETS_PER_PAYLOAD];
    uint meshletCount;
};

taskPayloadSharedEXT MeshletPayload payload;

shared vec3 sharedPositions[MESHLET_MAX_VERTICES];

uvec3 unpackPrimitive(uint packed) {
    return uvec3(
        packed & 0xFFu,
        (packed >> 8) & 0xFFu,
        (packed >> 16) & 0xFFu
    );
}

void unpackMeshletCounts(uint packed, out uint vertexCount, out uint primitiveCount) {
    vertexCount = packed & 0xFFu;
    primitiveCount = (packed >> 8) & 0xFFu;
}

void main() {
    uint payloadMeshletIndex = gl_WorkGroupID.x;
    if (payloadMeshletIndex >= payload.meshletCount) {
        SetMeshOutputsEXT(0, 0);
        return;
    }

    uint globalMeshletIndex = payload.meshletIndices[payloadMeshletIndex];
    uint drawIndex = payload.drawIndex;
    GPUMeshlet meshlet = meshlets[globalMeshletIndex];
    PerDrawData drawData = perDrawData[drawIndex];

    uint vertexCount, primitiveCount;
    unpackMeshletCounts(meshlet.vertexPrimCount, vertexCount, primitiveCount);
    SetMeshOutputsEXT(vertexCount, primitiveCount);

    mat4 modelMatrix = drawData.modelMatrix;
    mat4 mvp = pc.lightViewProjection * modelMatrix;

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

            // Apply GPU skinning if this is an animated mesh
            if (drawData.boneMatrixOffset != 0xFFFFFFFFu) {
                // Read bone indices (stored as floats, need to reinterpret as ints)
                ivec4 boneIndices = ivec4(
                    floatBitsToInt(vertexData[baseIdx + 8]),
                    floatBitsToInt(vertexData[baseIdx + 9]),
                    floatBitsToInt(vertexData[baseIdx + 10]),
                    floatBitsToInt(vertexData[baseIdx + 11])
                );
                vec4 boneWeights = vec4(
                    vertexData[baseIdx + 12],
                    vertexData[baseIdx + 13],
                    vertexData[baseIdx + 14],
                    vertexData[baseIdx + 15]
                );

                mat4 skinMatrix = mat4(0.0);
                float totalWeight = 0.0;
                for (int i = 0; i < 4; ++i) {
                    int boneIdx = boneIndices[i];
                    float weight = boneWeights[i];
                    if (boneIdx >= 0 && weight > 0.0) {
                        uint globalBoneIdx = drawData.boneMatrixOffset + uint(boneIdx);
                        skinMatrix += boneMatrices[globalBoneIdx] * weight;
                        totalWeight += weight;
                    }
                }

                if (totalWeight > 0.0) {
                    position = (skinMatrix * vec4(position, 1.0)).xyz;
                }
            }

            sharedPositions[localVertexIndex] = position;
        }
    }

    barrier();

    // Output positions only - depth-only rendering
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
