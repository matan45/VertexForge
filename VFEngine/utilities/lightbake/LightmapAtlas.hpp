#pragma once
#include "../math/RayBVH.hpp"
#include "../resource/Types.hpp"
#include <vector>
#include <cstdint>
#include <functional>

namespace lightbake
{
    // Mapping from a lightmap texel to world-space surface data
    struct TexelSample
    {
        glm::vec3 worldPosition{0.0f};
        glm::vec3 worldNormal{0.0f};
        uint32_t triangleIdx = UINT32_MAX; // Invalid = texel not mapped
        bool valid = false;
    };

    // Configuration for lightmap atlas generation
    struct LightmapConfig
    {
        float texelsPerUnit = 16.0f;
        uint32_t padding = 2;         // Texel padding between charts
        uint32_t minResolution = 4;   // Min resolution per entity
        uint32_t maxResolution = 2048; // Max resolution per entity
        uint32_t maxAtlasSize = 4096;  // Max atlas dimension
    };

    // Generates a lightmap UV atlas from scene triangles.
    // Maps each texel to a world-space position and normal for sampling.
    class LightmapAtlas
    {
    public:
        LightmapAtlas() = default;

        // Build the atlas from a built RayBVH.
        // Groups triangles by entity, computes per-entity chart sizes,
        // and packs them into a power-of-2 atlas.
        // Returns true on success.
        bool build(const math::RayBVH& bvh, const LightmapConfig& config = {});

        // Get the generated lightmap data (atlas dimensions + empty texels ready for baking)
        const resource::LightmapData& getLightmapData() const { return lightmapData_; }
        resource::LightmapData& getLightmapData() { return lightmapData_; }

        // Get the texel-to-world mapping for the baker
        const std::vector<TexelSample>& getTexelSamples() const { return texelSamples_; }

        // Get atlas dimensions
        uint32_t getWidth() const { return lightmapData_.width; }
        uint32_t getHeight() const { return lightmapData_.height; }

        bool isBuilt() const { return lightmapData_.width > 0 && lightmapData_.height > 0; }

        void clear();

        // Serialize lightmap data to file
        static bool save(const resource::LightmapData& data, const std::string& path);

        // Load lightmap data from file
        static resource::LightmapData load(const std::string& path);

    private:
        resource::LightmapData lightmapData_;
        std::vector<TexelSample> texelSamples_; // One per texel (width * height)

        // Per-entity chart for atlas packing
        struct EntityChart
        {
            uint32_t entityId;
            uint32_t width;
            uint32_t height;
            float surfaceArea;
            std::vector<uint32_t> triangleIndices; // Indices into RayBVH triangles
        };

        // Skyline bin-packing node
        struct SkylineNode
        {
            uint32_t x, y, width;
        };

        // Compute required resolution for an entity based on its surface area
        uint32_t computeChartResolution(float surfaceArea, const LightmapConfig& config) const;

        // Round up to next power of 2
        static uint32_t nextPowerOf2(uint32_t v);

        // Skyline bin-packing: try to place a rect of given size
        bool skylineInsert(std::vector<SkylineNode>& skyline, uint32_t rectW, uint32_t rectH,
                           uint32_t atlasW, uint32_t atlasH, uint32_t& outX, uint32_t& outY);

        // Rasterize triangles into the texel sample buffer for a chart region
        void rasterizeChart(const EntityChart& chart, const math::RayBVH& bvh,
                            uint32_t atlasX, uint32_t atlasY,
                            uint32_t chartW, uint32_t chartH);
    };
}
