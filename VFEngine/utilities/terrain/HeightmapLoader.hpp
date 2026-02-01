#pragma once

#include <string>
#include <vector>
#include <functional>
#include <optional>

namespace terrain
{
    // Loaded heightmap data normalized to [0, 1] range
    struct HeightmapData
    {
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<float> heights;  // Row-major, normalized [0, 1]

        [[nodiscard]] bool isValid() const { return width > 0 && height > 0 && !heights.empty(); }

        // Sample height at UV coordinates [0, 1] with bilinear interpolation
        [[nodiscard]] float sample(float u, float v) const;
    };

    // Loads heightmap from various formats and normalizes to [0, 1]
    class HeightmapLoader
    {
    public:
        // Load heightmap from file path
        // Supported formats: .vfImage, .raw (16-bit)
        static std::optional<HeightmapData> load(const std::string& filePath);

    private:
        // Load 16-bit RAW heightmap (assumes square, little-endian)
        static std::optional<HeightmapData> loadRawHeightmap(const std::string& filePath);

        // Load VF custom image format
        static std::optional<HeightmapData> loadVFImage(const std::string& filePath);

        // Get file extension (lowercase)
        static std::string getExtension(const std::string& filePath);
    };

    // Creates a height sampler function from heightmap data
    // Maps world coordinates to heightmap UV based on terrain dimensions
    using HeightSampler = std::function<float(float worldX, float worldZ)>;

    HeightSampler createHeightSamplerFromMap(
        const HeightmapData& heightmap,
        float terrainMinX,
        float terrainMinZ,
        float terrainWidth,
        float terrainDepth,
        float minHeight,
        float maxHeight
    );

} // namespace terrain
