#pragma once

#include <glm/glm.hpp>
#include <cstdint>
#include <cstddef>

namespace render::lighting
{
    // Light buffer capacity limits
    // Memory budget: ~66KB device + ~66KB staging = ~132KB total
    //   - 64 directional × 32 bytes = 2KB
    //   - 1024 point × 32 bytes = 32KB
    //   - 512 spot × 64 bytes = 32KB
    //
    // These limits support typical game scenes. For larger scenes with many lights,
    // consider implementing frustum culling to submit only visible lights, or
    // increase limits if GPU memory permits.
    namespace LightConstants
    {
        inline constexpr uint32_t MAX_DIRECTIONAL_LIGHTS = 64;   // Global lights, typically few needed
        inline constexpr uint32_t MAX_POINT_LIGHTS = 1024;       // Most common dynamic light type
        inline constexpr uint32_t MAX_SPOT_LIGHTS = 512;         // Used for flashlights, lamps, etc.
    }

    struct alignas(16) GPUDirectionalLight
    {
        glm::vec3 direction;      // 12 bytes - normalized direction vector
        float intensity;          // 4 bytes
        glm::vec3 color;          // 12 bytes
        uint32_t padding;         // 4 bytes
    };
    static_assert(sizeof(GPUDirectionalLight) == 32, "GPUDirectionalLight must be 32 bytes");
    static_assert(offsetof(GPUDirectionalLight, direction) == 0, "GPUDirectionalLight::direction offset mismatch");
    static_assert(offsetof(GPUDirectionalLight, intensity) == 12, "GPUDirectionalLight::intensity offset mismatch");
    static_assert(offsetof(GPUDirectionalLight, color) == 16, "GPUDirectionalLight::color offset mismatch");
    static_assert(offsetof(GPUDirectionalLight, padding) == 28, "GPUDirectionalLight::padding offset mismatch");

    struct alignas(16) GPUPointLight
    {
        glm::vec3 position;       // 12 bytes - world position
        float radius;             // 4 bytes - light influence radius
        glm::vec3 color;          // 12 bytes
        float intensity;          // 4 bytes
    };
    static_assert(sizeof(GPUPointLight) == 32, "GPUPointLight must be 32 bytes");
    static_assert(offsetof(GPUPointLight, position) == 0, "GPUPointLight::position offset mismatch");
    static_assert(offsetof(GPUPointLight, radius) == 12, "GPUPointLight::radius offset mismatch");
    static_assert(offsetof(GPUPointLight, color) == 16, "GPUPointLight::color offset mismatch");
    static_assert(offsetof(GPUPointLight, intensity) == 28, "GPUPointLight::intensity offset mismatch");

    struct alignas(16) GPUSpotLight
    {
        glm::vec3 position;       // 12 bytes - world position
        float range;              // 4 bytes
        glm::vec3 direction;      // 12 bytes - normalized direction
        float intensity;          // 4 bytes
        glm::vec3 color;          // 12 bytes
        float cosInnerAngle;      // 4 bytes - precomputed cos(innerAngle)
        float cosOuterAngle;      // 4 bytes - precomputed cos(outerAngle)
        float padding[3];         // 12 bytes - align to 64 bytes
    };
    static_assert(sizeof(GPUSpotLight) == 64, "GPUSpotLight must be 64 bytes");
    static_assert(offsetof(GPUSpotLight, position) == 0, "GPUSpotLight::position offset mismatch");
    static_assert(offsetof(GPUSpotLight, range) == 12, "GPUSpotLight::range offset mismatch");
    static_assert(offsetof(GPUSpotLight, direction) == 16, "GPUSpotLight::direction offset mismatch");
    static_assert(offsetof(GPUSpotLight, intensity) == 28, "GPUSpotLight::intensity offset mismatch");
    static_assert(offsetof(GPUSpotLight, color) == 32, "GPUSpotLight::color offset mismatch");
    static_assert(offsetof(GPUSpotLight, cosInnerAngle) == 44, "GPUSpotLight::cosInnerAngle offset mismatch");
    static_assert(offsetof(GPUSpotLight, cosOuterAngle) == 48, "GPUSpotLight::cosOuterAngle offset mismatch");
    static_assert(offsetof(GPUSpotLight, padding) == 52, "GPUSpotLight::padding offset mismatch");

    struct alignas(16) GPULightCounts
    {
        uint32_t directionalCount;
        uint32_t pointCount;
        uint32_t spotCount;
        uint32_t padding;
    };
    static_assert(sizeof(GPULightCounts) == 16, "GPULightCounts must be 16 bytes");
    static_assert(offsetof(GPULightCounts, directionalCount) == 0, "GPULightCounts::directionalCount offset mismatch");
    static_assert(offsetof(GPULightCounts, pointCount) == 4, "GPULightCounts::pointCount offset mismatch");
    static_assert(offsetof(GPULightCounts, spotCount) == 8, "GPULightCounts::spotCount offset mismatch");
    static_assert(offsetof(GPULightCounts, padding) == 12, "GPULightCounts::padding offset mismatch");
}
