#pragma once
#include <string>
#include <cstdint>

namespace lightbake
{
    struct LightmapConfig;
}

namespace services
{
    struct LightBakeResult
    {
        bool success = false;
        std::string lightmapPath;
        uint32_t atlasWidth = 0;
        uint32_t atlasHeight = 0;
        uint32_t bakedLightCount = 0;
        float bakeTimeSeconds = 0.0f;
    };

    struct LightBakeConfig
    {
        float texelsPerUnit = 16.0f;
        uint32_t maxAtlasSize = 4096;
        std::string outputPath;  // Where to save the .vfLightmap
    };

    class ILightBakeProvider
    {
    public:
        virtual ~ILightBakeProvider() = default;

        virtual void startBake(const LightBakeConfig& config) = 0;
        virtual void cancelBake() = 0;
        virtual float getBakeProgress() const = 0;
        virtual bool isBaking() const = 0;
        virtual LightBakeResult getResult() const = 0;
    };
}
