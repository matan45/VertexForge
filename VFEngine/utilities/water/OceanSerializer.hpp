#pragma once

#include <glm/glm.hpp>
#include <string>
#include <cstdint>

namespace ocean
{
    struct OceanFileData
    {
        float waterHeight = 0.0f;
        bool physicsEnabled = true;

        // Visual
        glm::vec4 shallowColor{0.0f, 0.4f, 0.6f, 0.7f};
        glm::vec4 deepColor{0.0f, 0.05f, 0.2f, 0.95f};
        float maxVisibleDepth = 10.0f;
        float fresnelPower = 5.0f;

        // Physics
        float density = 1000.0f;
        float drag = 0.5f;
        float buoyancyStrength = 2.0f;

        // Ocean FFT
        uint32_t resolution = 256;
        float patchSize = 100.0f;
        float windSpeed = 8.0f;
        float windDirection = 45.0f;
        float amplitude = 0.00003f;
        float choppiness = 1.2f;
        float foamThreshold = -0.1f;
        float displacementScale = 4.0f;
    };

    class OceanSerializer
    {
    public:
        static bool save(const std::string& path, const OceanFileData& data);
        static bool load(const std::string& path, OceanFileData& outData);
    };
}
