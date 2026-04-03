#pragma once

#include <glm/glm.hpp>
#include <cstdint>

namespace render::volumetric
{
    enum class VolumetricQuality : uint8_t
    {
        Low = 0,
        Medium,
        High
    };

    struct VolumetricGridDimensions
    {
        uint32_t width;
        uint32_t height;
        uint32_t depth;

        static VolumetricGridDimensions fromQuality(VolumetricQuality quality)
        {
            switch (quality)
            {
            case VolumetricQuality::Low:    return {80, 45, 64};
            case VolumetricQuality::Medium: return {160, 90, 128};
            case VolumetricQuality::High:   return {240, 135, 128};
            default:                        return {160, 90, 128};
            }
        }
    };

    struct alignas(16) GPUVolumetricParams
    {
        glm::uvec4 gridDimensions{0};       // xyz = width, height, depth
        glm::vec4 depthParams{0.0f};         // x = near, y = far, z = log(far/near), w = 1/log(far/near)
        glm::mat4 invViewProjection{1.0f};   // 64 bytes
        glm::mat4 prevViewProjection{1.0f};  // 64 bytes
        glm::vec4 fogParams{0.0f};           // x = uniformDensity, y = heightFogDensity, z = heightFogFalloff, w = heightFogOffset
        glm::vec4 scatterParams{0.0f};       // x = scatteringCoeff, y = absorptionCoeff, z = anisotropy (HG g), w = maxDistance
        glm::vec4 fogColor{0.8f, 0.85f, 0.9f, 1.0f}; // rgb = fog color, a = intensity
        glm::vec4 ambientParams{0.0f};       // x = ambientIntensity, y = temporalBlendFactor, z = frameIndex (as float), w = giInjectionIntensity
        glm::vec4 cameraPosition{0.0f};      // xyz = world pos, w = earlyTerminationThreshold
        glm::vec4 noiseParams{0.0f};         // x = scale, y = intensity, z = timeOffset, w = octaves
    };
    static_assert(sizeof(GPUVolumetricParams) == 256, "GPUVolumetricParams size mismatch");

    static constexpr uint32_t MAX_FOG_VOLUMES = 64;

    struct alignas(16) GPUFogVolume
    {
        glm::mat4 worldToLocal{1.0f};                          // 64 bytes
        glm::vec4 boundsMin{0.0f};                              // 16 bytes (world AABB min)
        glm::vec4 boundsMax{0.0f};                              // 16 bytes (world AABB max)
        glm::vec4 albedoAndDensity{0.8f, 0.85f, 0.9f, 0.5f};  // 16 bytes
        glm::vec4 emissionAndFalloff{0.0f, 0.0f, 0.0f, 0.5f}; // 16 bytes
        uint32_t shapeType = 0;                                 // 4 bytes
        uint32_t blendMode = 0;                                 // 4 bytes
        int32_t densityTextureIndex = -1;                       // 4 bytes (stub: always -1)
        uint32_t padding = 0;                                   // 4 bytes
    };
    static_assert(sizeof(GPUFogVolume) == 144, "GPUFogVolume size mismatch");
}
