#ifndef CAMERA_TYPES_GLSL
#define CAMERA_TYPES_GLSL

// Must match CameraUBO in MeshTypes.hpp (240 bytes)
struct CameraData {
    mat4 view;
    mat4 projection;
    vec3 cameraPos;
    float time;
    vec4 frustumPlanes[6];
};

// Must match GPUCameraData in GPUDrivenTypes.hpp (432 bytes)
struct GPUCameraData {
    mat4 view;
    mat4 projection;
    mat4 viewProjection;
    mat4 invViewProjection;

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
    uint padding1;
    uint padding2;
};

#endif // CAMERA_TYPES_GLSL
