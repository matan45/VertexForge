#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <functional>
#include <glm/glm.hpp>

namespace importTypes
{
    struct ImposterAtlasConfig
    {
        uint32_t horizontalAngles = 8;    // Views around Y axis (evenly spaced)
        uint32_t verticalAngles = 3;       // Views at different elevations (e.g., 0, 30, 60 degrees)
        uint32_t viewResolution = 256;     // Resolution per view in the atlas
        float meshScale = 1.0f;            // Scale factor for the mesh
        bool generateNormalMap = true;      // Also capture normals
    };

    struct ImposterViewInfo
    {
        float horizontalAngle;   // Radians around Y
        float verticalAngle;     // Radians elevation
        glm::vec4 uvRect;        // x,y = offset, z,w = size in atlas UV space
    };

    struct ImposterAtlasData
    {
        uint32_t atlasWidth = 0;
        uint32_t atlasHeight = 0;
        std::vector<uint8_t> colorData;     // RGBA8
        std::vector<uint8_t> normalData;    // RGBA8 (optional)
        std::vector<ImposterViewInfo> views;
        ImposterAtlasConfig config;
    };

    using ImposterProgressCallback = std::function<void(float progress)>;

    class ImposterAtlasGenerator
    {
    public:
        // Generate imposter atlas layout from config
        // This is a CPU-side placeholder - actual GPU rendering will be wired later
        // For now, creates atlas layout metadata and placeholder textures
        static ImposterAtlasData generateAtlasLayout(const ImposterAtlasConfig& config);

        // Calculate atlas dimensions needed for the given config
        static glm::uvec2 calculateAtlasDimensions(const ImposterAtlasConfig& config);
    };
}
