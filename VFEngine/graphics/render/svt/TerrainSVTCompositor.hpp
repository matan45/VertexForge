#pragma once

#include "SVTTypes.hpp"
#include "SVTStreamManager.hpp"
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <memory>
#include <functional>

namespace resource
{
    struct TextureData;
}

namespace render::svt
{
    // Terrain layer source data for compositing
    struct TerrainLayerSource
    {
        std::shared_ptr<resource::TextureData> albedoTexture;
        std::shared_ptr<resource::TextureData> normalTexture;
        std::shared_ptr<resource::TextureData> ormTexture;
        float tilingScale = 1.0f;
        float roughness = 0.5f;    // Scalar fallback
        float metallic = 0.0f;
        float ao = 1.0f;
    };

    // Weight map data for a terrain tile region
    struct TerrainWeightMapRegion
    {
        const uint8_t* data = nullptr;  // RGBA packed weights
        uint32_t resolution = 0;        // Weight map texels per side
        glm::vec2 worldMin{0.0f};       // World-space bounds of this weight map
        glm::vec2 worldMax{0.0f};
        uint32_t packedLayerIndices = 0; // 4x8-bit palette indices packed
    };

    // Generates SVT tile data for terrain by compositing terrain layer textures
    // weighted by splat maps. Implements SVTTileProvider interface.
    class TerrainSVTCompositor : public SVTTileProvider
    {
    private:
        SVTConfig config_;

        // Terrain world bounds (for virtual UV mapping)
        glm::vec2 terrainWorldMin_{0.0f};
        glm::vec2 terrainWorldMax_{0.0f};

        // Layer sources (up to 32)
        std::vector<TerrainLayerSource> layers_;

        // Callback to retrieve weight map data for a world region
        using WeightMapCallback = std::function<TerrainWeightMapRegion(const glm::vec2& worldMin, const glm::vec2& worldMax)>;
        WeightMapCallback weightMapCallback_;

        bool initialized_ = false;

    public:
        TerrainSVTCompositor() = default;

        void init(const SVTConfig& config,
                  const glm::vec2& terrainWorldMin,
                  const glm::vec2& terrainWorldMax);

        void setLayers(std::vector<TerrainLayerSource> layers);
        void setWeightMapCallback(WeightMapCallback callback);

        // SVTTileProvider interface
        SVTTileData generateTile(const VirtualTileCoord& coord) override;

        // Compute the SVT scale/offset for mapping world XZ to virtual UV
        glm::vec2 getSVTScale() const;
        glm::vec2 getSVTOffset() const;

    private:
        // Sample a terrain layer texture at world-space coordinates
        glm::vec4 sampleLayerTexture(const resource::TextureData* texture,
                                      float worldX, float worldZ,
                                      float tilingScale) const;

        // Sample weight map at world coordinates
        void sampleWeights(const TerrainWeightMapRegion& region,
                           float worldX, float worldZ,
                           float weights[4]) const;

        // Compress RGBA8 tile data to BC7
        std::vector<uint8_t> compressTileBC7(const std::vector<uint8_t>& rgbaData,
                                              bool srgb) const;
    };
}
