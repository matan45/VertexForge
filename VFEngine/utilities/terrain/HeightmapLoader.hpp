#pragma once
#include "TerrainExport.hpp"

#include "TerrainTypes.hpp"
#include <string>
#include <vector>
#include <memory>

namespace terrain
{
    // Heights normalized to [0, 1] range
#pragma warning(push)
#pragma warning(disable: 4251)
    struct VF_TERRAIN_API HeightmapData
    {
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<float> heights; // Row-major, normalized [0, 1]

        [[nodiscard]] bool isValid() const { return width > 0 && height > 0 && !heights.empty(); }

        [[nodiscard]] float sample(float u, float v) const;
    };
#pragma warning(pop)

    class VF_TERRAIN_API HeightmapLoader
    {
    public:
        // Supported formats: .vfImage
        static std::shared_ptr<HeightmapData> load(const std::string& filePath);

    private:
        static std::shared_ptr<HeightmapData> loadVFImage(const std::string& filePath);
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
    VF_TERRAIN_API HeightSampler createHeightSamplerFromMap(
        std::shared_ptr<const HeightmapData> heightmap,
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
    VF_TERRAIN_API HeightSampler createCompositeHeightSampler(
        const std::vector<HeightmapRegion>& regions,
        float worldTileSize,
        float minHeight,
        float maxHeight
    );
}
