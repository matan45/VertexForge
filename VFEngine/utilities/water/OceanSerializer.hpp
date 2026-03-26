#pragma once

#include "data/OceanData.hpp"
#include <string>

namespace ocean
{
    struct OceanFileData
    {
        float waterHeight = 0.0f;
        bool physicsEnabled = true;

        services::OceanVisualSettings visual;
        services::OceanPhysicsSettings physics;
        services::OceanFFTConfigData fftConfig;
    };

    class OceanSerializer
    {
    public:
        static bool save(const std::string& path, const OceanFileData& data);
        static bool load(const std::string& path, OceanFileData& outData);
    };
}
