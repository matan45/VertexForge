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

    struct TerrainBounds
    {
        float minX = 0.0f;
        float minZ = 0.0f;
        float width = 0.0f;
        float depth = 0.0f;
        float minHeight = 0.0f;
        float maxHeight = 0.0f;
    };

    // Maps world coordinates to heightmap UV based on terrain dimensions
    HeightSampler createHeightSamplerFromMap(
        std::shared_ptr<const HeightmapData> heightmap,
        const TerrainBounds& bounds
    );

    // Creates a streaming height sampler from a .vfSVT file.
    // Only reads and decompresses tiles on demand (caches recent tiles).
    // Returns empty sampler if file can't be opened.
    HeightSampler createStreamingHeightSamplerFromSVT(
        const std::string& svtPath,
        const TerrainBounds& bounds
    );

    // A rectangular tile region covered by a single heightmap file
    struct HeightmapRegion
    {
        std::string filePath;
        int32_t tileMinX = 0;
        int32_t tileMinZ = 0;
        int32_t tileMaxX = 0;
        int32_t tileMaxZ = 0;
    };

    // Creates a composite sampler from multiple heightmap regions.
    // Each region maps its heightmap to its tile range. For overlapping regions,
    // the last region in the vector wins. Uncovered tiles return minHeight (flat).
    HeightSampler createCompositeHeightSampler(
        const std::vector<HeightmapRegion>& regions,
        float worldTileSize,
        float minHeight,
        float maxHeight
    );
}
