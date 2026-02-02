#pragma once

#include "TerrainTile.hpp"
#include "TerrainTileGenerator.hpp"
#include <unordered_map>
#include <vector>
#include <optional>
#include <memory>

namespace terrain
{
    // Manages a grid of terrain tiles with spatial organization
    class TerrainGrid
    {
    public:
        explicit TerrainGrid(const TerrainTileConfig& config);
        ~TerrainGrid() = default;

        // Configuration
        void setConfig(const TerrainTileConfig& config);
        [[nodiscard]] const TerrainTileConfig& getConfig() const { return config_; }

        // Set height sampler for tile generation
        void setHeightSampler(HeightSampler sampler);

        // Tile management
        [[nodiscard]] TerrainTile* getTile(const TileCoord& coord);
        [[nodiscard]] const TerrainTile* getTile(const TileCoord& coord) const;
        [[nodiscard]] TerrainTile* getOrCreateTile(const TileCoord& coord);
        void removeTile(const TileCoord& coord);
        void clear();

        // Coordinate conversion
        [[nodiscard]] TileCoord worldToTileCoord(float worldX, float worldZ) const;
        [[nodiscard]] glm::vec2 tileCoordToWorld(const TileCoord& coord) const;
        [[nodiscard]] glm::vec3 tileCoordToWorldCenter(const TileCoord& coord) const;

        // Spatial queries
        [[nodiscard]] std::vector<TerrainTile*> getTilesInRadius(
            const glm::vec3& center,
            float radius
        );

        [[nodiscard]] std::vector<TerrainTile*> getVisibleTiles(
            const math::Frustum& frustum
        );

        [[nodiscard]] std::vector<TerrainTile*> getTilesInAABB(
            const math::AABB& bounds
        );

        // Height queries across tiles
        [[nodiscard]] std::optional<float> sampleHeight(float worldX, float worldZ) const;

        // Neighbor management
        void updateNeighborReferences(TerrainTile& tile);
        void updateAllNeighborReferences();

        // LOD updates based on camera position
        // Returns coordinates of tiles whose state changed (LOD, visibility, dirty flags)
        [[nodiscard]] std::vector<TileCoord> updateLODs(const glm::vec3& cameraPosition);

        // Regenerate dirty tiles
        void regenerateDirtyTiles(ProgressCallback progress = nullptr);

        // Accessors
        [[nodiscard]] std::vector<TerrainTile*> getAllTiles();
        [[nodiscard]] std::vector<const TerrainTile*> getAllTiles() const;
        [[nodiscard]] size_t getTileCount() const { return tiles_.size(); }
        [[nodiscard]] bool hasTile(const TileCoord& coord) const;

        // World bounds of all tiles
        [[nodiscard]] math::AABB getWorldBounds() const;

        // Get the tile generator
        [[nodiscard]] TerrainTileGenerator& getGenerator() { return *generator_; }
        [[nodiscard]] const TerrainTileGenerator& getGenerator() const { return *generator_; }

        // Create a rectangular grid of tiles
        void createGrid(int32_t minX, int32_t minZ, int32_t maxX, int32_t maxZ,
                        ProgressCallback progress = nullptr);

    private:
        TerrainTileConfig config_;
        std::unique_ptr<TerrainTileGenerator> generator_;
        std::unordered_map<TileCoord, std::unique_ptr<TerrainTile>, TileCoordHash> tiles_;

        // Cache for world bounds
        mutable math::AABB cachedWorldBounds_;
        mutable bool boundsDirty_ = true;

        void invalidateBoundsCache();
    };

} // namespace terrain
