#pragma once

#include <glm/glm.hpp>

namespace render::lighting
{
    namespace LightConstants
    {
        inline constexpr uint32_t MAX_DIRECTIONAL_LIGHTS = 64;
        inline constexpr uint32_t MAX_POINT_LIGHTS = 1024;
        inline constexpr uint32_t MAX_SPOT_LIGHTS = 512;
    }

    struct alignas(16) GPUDirectionalLight
    {
        glm::vec3 direction;
        float intensity;
        glm::vec3 color;
        int32_t shadowIndex;  // -1 = no shadow
        int32_t shadowMode;   // 0 = cascade, 1 = clipmap
        uint32_t padding[3];
    };
    static_assert(sizeof(GPUDirectionalLight) == 48);

    struct alignas(16) GPUPointLight
    {
        glm::vec3 position;
        float radius;
        glm::vec3 color;
        float intensity;
        int32_t shadowIndex;  // -1 = no shadow
        uint32_t padding[3];
    };
    static_assert(sizeof(GPUPointLight) == 48);

    struct alignas(16) GPUSpotLight
    {
        glm::vec3 position;
        float range;
        glm::vec3 direction;
        float intensity;
        glm::vec3 color;
        float cosInnerAngle;
        float cosOuterAngle;
        int32_t shadowIndex;  // -1 = no shadow
        uint32_t padding[2];
    };
    static_assert(sizeof(GPUSpotLight) == 64);

    struct alignas(16) GPULightCounts
    {
        uint32_t directionalCount;
        uint32_t pointCount;
        uint32_t spotCount;
        float shadowIntensity;
        uint32_t rtShadowActive;  // 1 = use RT for directional shadows, 0 = use VSM
        uint32_t pad[3];
    };
    static_assert(sizeof(GPULightCounts) == 32);
}
