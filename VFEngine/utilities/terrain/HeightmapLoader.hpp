#pragma once

#include "TerrainTypes.hpp"
#include <string>
#include <vector>
#include <memory>

namespace terrain
{
    // Heights normalized to [0, 1] range
    struct HeightmapData
    {
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<float> heights; // Row-major, normalized [0, 1]

        [[nodiscard]] bool isValid() const { return width > 0 && height > 0 && !heights.empty(); }

        [[nodiscard]] float sample(float u, float v) const;
    };

    class HeightmapLoader
    {
    public:
        // Supported formats: .vfImage, .vfSVT
        static std::shared_ptr<HeightmapData> load(const std::string& filePath);

    private:
        static std::shared_ptr<HeightmapData> loadVFImage(const std::string& filePath);
        static std::shared_ptr<HeightmapData> loadVFSVT(const std::string& filePath);
        static std::string getExtension(const std::string& filePath);
    };

    // Maps world coordinates to heightmap UV based on terrain dimensions
    HeightSampler createHeightSamplerFromMap(
        std::shared_ptr<const HeightmapData> heightmap,
        float terrainMinX,
        float terrainMinZ,
        float terrainWidth,
        float terrainDepth,
        float minHeight,
        float maxHeight
    );
}
