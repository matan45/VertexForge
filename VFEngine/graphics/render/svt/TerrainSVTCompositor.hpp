#pragma once

#include "SVTTypes.hpp"
#include "SVTStreamManager.hpp"
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <memory>

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

    // UV and world-space bounds for a virtual tile
    struct TileBounds
    {
        float uvMinX = 0.0f;
        float uvMinY = 0.0f;
        float texelUVSize = 0.0f;
        uint32_t physTileSize = 0;
        glm::vec2 worldSize{0.0f};
    };

    // Generates SVT tile data for terrain by compositing terrain layer textures
    // weighted by splat maps. Implements SVTTileProvider interface.
    class TerrainSVTCompositor : public SVTTileProvider
    {
    private:
        SVTConfig config;

        // Terrain world bounds (for virtual UV mapping)
        glm::vec2 terrainWorldMin{0.0f};
        glm::vec2 terrainWorldMax{0.0f};

        // Layer sources (up to 32)
        std::vector<TerrainLayerSource> layers;

        bool initialized = false;

    public:
        TerrainSVTCompositor() = default;

        void init(const SVTConfig& config,
                  const glm::vec2& terrainWorldMin,
                  const glm::vec2& terrainWorldMax);

        void setLayers(std::vector<TerrainLayerSource> layers);

        // SVTTileProvider interface
        SVTTileData generateTile(const VirtualTileCoord& coord) override;

        // Compute the SVT scale/offset for mapping world XZ to virtual UV
        glm::vec2 getSVTScale() const;
        glm::vec2 getSVTOffset() const;

    private:
        // Compute UV and world-space bounds for a virtual tile coordinate
        TileBounds computeTileBounds(const VirtualTileCoord& coord) const;

        // Composite layer textures into RGBA8 buffers for a tile
        void compositeTileTexels(const TileBounds& bounds,
                                 const TerrainWeightMapRegion& weightRegion,
                                 std::vector<uint8_t>& albedoRGBA,
                                 std::vector<uint8_t>& normalRGBA,
                                 std::vector<uint8_t>& ormRGBA) const;

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
