#type TASK
#version 460 core
#extension GL_EXT_mesh_shader : require

// GPU-Driven Task Shader
// Processes meshlets for a single object, performs per-meshlet culling,
// and emits visible meshlets to mesh shader via payload.

layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;

// Constants
const uint TASK_WORKGROUP_SIZE = 32;
const uint MAX_MESHLETS_PER_PAYLOAD = 32;

// ============================================================================
// Per-Draw Data (240 bytes, must match PerDrawData in GPUDrivenTypes.hpp)
// ============================================================================
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
    uint meshletOffset;      // First meshlet index in meshlet buffer
    uint meshletCount;       // Number of meshlets for selected LOD

    uint baseVertexOffset;   // Base vertex offset in merged vertex buffer
    uint padding1;
    uint padding2;
    uint padding3;
};

// GPUMeshlet structure (48 bytes, must match GPUMeshlet in MeshletBufferTypes.hpp)
struct GPUMeshlet {
    uint vertexOffset;       // Offset into meshlet vertex index buffer
    uint primitiveOffset;    // Offset into meshlet primitive buffer
    uint vertexCount;        // Packed: (vertexCount | primitiveCount << 8)
    uint globalVertexOffset; // Base vertex offset in merged vertex buffer

    vec4 boundingSphere;     // xyz = center (local space), w = radius
    vec4 cone;               // xyz = cone axis, w = cos(half-angle), >= 1.0 means no backface culling
};

// Camera/Frustum data
struct CameraData {
    mat4 view;
    mat4 projection;
    mat4 viewProjection;
    mat4 invViewProjection;

    vec4 cameraPosition;     // xyz = position, w = nearPlane
    vec4 screenParams;       // xy = resolution, zw = 1/resolution

    vec4 frustumPlanes[6];   // Frustum planes for culling

    float farPlane;
    uint objectCount;
    uint hiZMipLevels;
    uint frameIndex;

    uint enableFrustumCulling;
    uint enableOcclusionCulling;
    uint enableLODSelection;
    uint batchCount;

    uint commandsPerBatch;
    uint shaderGroupCount;
    uint padding1;
    uint padding2;
};

// ============================================================================
// Descriptor Bindings
// ============================================================================

// Set 0: Camera UBO
layout(set = 0, binding = 0) uniform CameraUBO {
    CameraData camera;
};

// Set 1: Per-draw data buffer
layout(std430, set = 1, binding = 0) readonly buffer PerDrawDataBuffer {
    PerDrawData perDrawData[];
};

// Set 3: Meshlet data
layout(std430, set = 3, binding = 0) readonly buffer MeshletBuffer {
    GPUMeshlet meshlets[];
};

// ============================================================================
// Task Payload (shared data passed to mesh shader)
// ============================================================================
struct MeshletPayload {
    uint drawIndex;                              // Index into per-draw data buffer
    uint meshletIndices[MAX_MESHLETS_PER_PAYLOAD]; // Global meshlet indices
    uint meshletCount;                           // Number of visible meshlets
};

taskPayloadSharedEXT MeshletPayload payload;

// Shared memory for compaction
shared uint sharedVisibleCount;
shared uint sharedMeshletIndices[TASK_WORKGROUP_SIZE];

// ============================================================================
// Helper Functions
// ============================================================================

// Transform bounding sphere from local to world space
vec4 transformBoundingSphere(vec4 localSphere, mat4 modelMatrix) {
    vec3 worldCenter = (modelMatrix * vec4(localSphere.xyz, 1.0)).xyz;

    float scaleX = length(modelMatrix[0].xyz);
    float scaleY = length(modelMatrix[1].xyz);
    float scaleZ = length(modelMatrix[2].xyz);
    float maxScale = max(max(scaleX, scaleY), scaleZ);

    float worldRadius = localSphere.w * maxScale;

    return vec4(worldCenter, worldRadius);
}

// Test sphere against frustum planes
bool sphereInFrustum(vec4 sphere, vec4 frustumPlanes[6]) {
    for (int i = 0; i < 6; i++) {
        float distance = dot(frustumPlanes[i].xyz, sphere.xyz) + frustumPlanes[i].w;
        if (distance < -sphere.w) {
            return false;
        }
    }
    return true;
}

// Test backface cone culling
// Returns true if meshlet is potentially visible (not back-facing)
bool coneCullTest(vec4 cone, mat4 modelMatrix, vec3 cameraPos, vec3 meshletCenter) {
    // If cone.w >= 1.0, no backface culling (e.g., for two-sided materials)
    if (cone.w >= 1.0) {
        return true;
    }

    // Transform cone axis to world space (use upper 3x3 of model matrix)
    vec3 worldConeAxis = normalize(mat3(modelMatrix) * cone.xyz);

    // Direction from camera to meshlet center
    vec3 viewDir = normalize(meshletCenter - cameraPos);

    // If the angle between view direction and cone axis is less than half-angle,
    // the meshlet is back-facing and can be culled
    float dotProduct = dot(viewDir, worldConeAxis);

    // visible if dot < cos(half-angle) (cone.w)
    // i.e., angle > half-angle
    return dotProduct < cone.w;
}

// ============================================================================
// Main
// ============================================================================
void main() {
    // Each workgroup processes meshlets for one draw command
    // gl_WorkGroupID.x = index within the task dispatch for this object
    // The draw index comes from the dispatch: each object dispatches ceil(meshletCount/32) task workgroups

    // Calculate which draw command this workgroup belongs to
    // This is computed by the mesh shader pipeline based on the indirect dispatch
    // For now, we use a push constant or compute the draw index from the batch structure

    // Get draw index - stored in gl_WorkGroupID based on dispatch structure
    // Each dispatch command corresponds to one visible object
    // The compute shader outputs one dispatch per object
    uint drawIndex = gl_DrawID; // Use gl_DrawID for indirect count dispatch

    PerDrawData drawData = perDrawData[drawIndex];

    // Calculate which meshlet this thread processes
    uint localMeshletIndex = gl_LocalInvocationID.x;
    uint workgroupMeshletBase = gl_WorkGroupID.x * TASK_WORKGROUP_SIZE;
    uint meshletIndex = workgroupMeshletBase + localMeshletIndex;

    // Initialize shared memory
    if (gl_LocalInvocationID.x == 0) {
        sharedVisibleCount = 0;
    }
    barrier();

    // Check if this meshlet is within the object's meshlet range
    bool isValidMeshlet = meshletIndex < drawData.meshletCount;
    bool isVisible = false;

    if (isValidMeshlet) {
        // Get the global meshlet index
        uint globalMeshletIndex = drawData.meshletOffset + meshletIndex;
        GPUMeshlet meshlet = meshlets[globalMeshletIndex];

        // Transform meshlet bounding sphere to world space
        vec4 worldSphere = transformBoundingSphere(meshlet.boundingSphere, drawData.modelMatrix);

        // Frustum culling
        if (camera.enableFrustumCulling != 0u) {
            isVisible = sphereInFrustum(worldSphere, camera.frustumPlanes);
        } else {
            isVisible = true;
        }

        // Backface cone culling (only if passed frustum)
        if (isVisible) {
            isVisible = coneCullTest(meshlet.cone, drawData.modelMatrix,
                                     camera.cameraPosition.xyz, worldSphere.xyz);
        }

        // If visible, add to shared memory for compaction
        if (isVisible) {
            uint slot = atomicAdd(sharedVisibleCount, 1);
            if (slot < TASK_WORKGROUP_SIZE) {
                sharedMeshletIndices[slot] = globalMeshletIndex;
            }
        }
    }

    barrier();

    // First thread writes payload and emits mesh tasks
    if (gl_LocalInvocationID.x == 0) {
        uint visibleCount = min(sharedVisibleCount, MAX_MESHLETS_PER_PAYLOAD);

        payload.drawIndex = drawIndex;
        payload.meshletCount = visibleCount;

        // Copy visible meshlet indices to payload
        for (uint i = 0; i < visibleCount; i++) {
            payload.meshletIndices[i] = sharedMeshletIndices[i];
        }

        // Emit mesh shader workgroups (one per visible meshlet)
        EmitMeshTasksEXT(visibleCount, 1, 1);
    }
}
