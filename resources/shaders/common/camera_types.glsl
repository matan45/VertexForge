#ifndef CAMERA_TYPES_GLSL
#define CAMERA_TYPES_GLSL

// Must match CameraUBO in CameraTypes.hpp (256 bytes)
struct CameraData {
    mat4 view;
    mat4 projection;
    vec3 cameraPos;
    float time;
    float snowAccumulation;
    float wetness;
    float disableShadows; // 1.0 = skip shadow sampling for this pass (e.g. RTT/minimap)
    float _pad2;
    vec4 frustumPlanes[6];
};

// Must match GPUCameraData in GPUDrivenTypes.hpp (528 bytes)
struct GPUCameraData {
    mat4 view;
    mat4 projection;
    mat4 viewProjection;
    mat4 invViewProjection;
    mat4 prevViewProjection;

    vec4 cameraPosition;
    vec4 screenParams;

    vec4 frustumPlanes[6];

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
    uint enableDistanceCulling;
    float globalLodBias;

    vec4 categoryDistSq0;   // [staticMesh, terrain, foliage, vfx] squared distances
    vec4 categoryDistSq1;   // [decals, 0, 0, shadowMultiplier]
};

// enableLODSelection values
const uint LOD_SELECTION_DISABLED       = 0u;
const uint LOD_SELECTION_ENABLED        = 1u;
const uint LOD_SELECTION_WITH_CROSSFADE = 2u;

#endif // CAMERA_TYPES_GLSL
