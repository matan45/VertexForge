#pragma once

#include <glm/glm.hpp>
#include <cstdint>
#include <cstddef>

namespace render::common
{
    struct CameraUBO
    {
        alignas(16) glm::mat4 view;
        alignas(16) glm::mat4 projection;
        alignas(16) glm::vec3 cameraPos;
        float time;
        float snowAccumulation;
        float wetness;
        float _pad[2];
        alignas(16) glm::vec4 frustumPlanes[6];
    };

    static_assert(sizeof(CameraUBO) == 256, "CameraUBO must be 256 bytes to match GLSL CameraData");
    static_assert(offsetof(CameraUBO, view) == 0, "CameraUBO::view offset mismatch");
    static_assert(offsetof(CameraUBO, projection) == 64, "CameraUBO::projection offset mismatch");
    static_assert(offsetof(CameraUBO, cameraPos) == 128, "CameraUBO::cameraPos offset mismatch");
    static_assert(offsetof(CameraUBO, time) == 140, "CameraUBO::time offset mismatch");
    static_assert(offsetof(CameraUBO, snowAccumulation) == 144, "CameraUBO::snowAccumulation offset mismatch");
    static_assert(offsetof(CameraUBO, frustumPlanes) == 160, "CameraUBO::frustumPlanes offset mismatch");

    struct alignas(16) GPUCameraData
    {
        glm::mat4 view;
        glm::mat4 projection;
        glm::mat4 viewProjection;
        glm::mat4 invViewProjection;
        glm::mat4 prevViewProjection;
        glm::vec4 cameraPosition;
        glm::vec4 screenParams;
        glm::vec4 frustumPlanes[6];
        float farPlane;
        uint32_t objectCount;
        uint32_t hiZMipLevels;
        uint32_t frameIndex;
        uint32_t enableFrustumCulling;
        uint32_t enableOcclusionCulling;
        uint32_t enableLODSelection;
        uint32_t batchCount;
        uint32_t commandsPerBatch;
        uint32_t shaderGroupCount;
        uint32_t enableDistanceCulling;
        float globalLodBias;
        glm::vec4 categoryDistSq0;     // [staticMesh^2, terrain^2, foliage^2, vfx^2]
        glm::vec4 categoryDistSq1;     // [decals^2, 0, 0, shadowMultiplier]
    };
    static_assert(sizeof(GPUCameraData) == 528, "GPUCameraData must be 528 bytes to match GLSL GPUCameraData");
}
