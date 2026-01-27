// Shared camera struct definitions - SINGLE SOURCE OF TRUTH
// Must match corresponding C++ structs in MeshTypes.hpp and GPUDrivenTypes.hpp
//
// Usage: #include "common/camera_types.glsl"
// Requires: #extension GL_GOOGLE_include_directive : require

#ifndef CAMERA_TYPES_GLSL
#define CAMERA_TYPES_GLSL

// Simple camera data for mesh/task shaders
// Must match CameraUBO in MeshTypes.hpp (240 bytes)
struct CameraData {
    mat4 view;
    mat4 projection;
    vec3 cameraPos;
    float time;
    vec4 frustumPlanes[6];
};

// Extended camera data for GPU culling compute shaders
// Must match GPUCameraData in GPUDrivenTypes.hpp (432 bytes)
struct GPUCameraData {
    mat4 view;
    mat4 projection;
    mat4 viewProjection;
    mat4 invViewProjection;

    vec4 cameraPosition;       // xyz = position, w = nearPlane
    vec4 screenParams;         // xy = resolution, zw = 1/resolution

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
    uint padding1;
    uint padding2;
};

#endif // CAMERA_TYPES_GLSL
