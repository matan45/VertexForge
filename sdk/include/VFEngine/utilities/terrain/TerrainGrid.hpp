#pragma once
#include "TerrainExport.hpp"

#include "TerrainTile.hpp"
#include "TerrainTileGenerator.hpp"
#include "TerrainSerializer.hpp"
#include "TerrainFileCache.hpp"
#include "TerrainHeightLayerStore.hpp"
#include "TerrainQuadtree.hpp"
#include <unordered_map>
#include <vector>
#include <memory>

namespace terrain
{
#pragma warning(push)
#pragma warning(disable: 4251)
    class VF_TERRAIN_API TerrainGrid
    {
    private:
        TerrainTileConfig config;
        std::unique_ptr<TerrainTileGenerator> generator;
        std::unordered_map<TileCoord, std::unique_ptr<TerrainTile>, TileCoordHash> tiles;
        std::shared_ptr<TerrainFileCache> fileCache;
        TerrainQuadtree quadtree;
        std::vector<TerrainTile*> lastVisibleTiles;

        // VK-1645. Authoritative base-height blocks + the reserved height-layer stack.
        //
        // Lives here, not on TerrainTile, because removeTile() destroys the whole tile while a
        // base block must survive stream-out (it has no persistence until VK-1646, so losing it
        // loses artist data). Dies with the grid, which is exactly the lifetime it wants.
        std::unique_ptr<TerrainHeightLayerStore> heightLayers;

    public:
        explicit TerrainGrid(const TerrainTileConfig& config);
        ~TerrainGrid() = default;

        void setHeightSampler(HeightSampler sampler);

        [[nodiscard]] TerrainTile* getTile(const TileCoord& coord);
        [[nodiscard]] const TerrainTile* getTile(const TileCoord& coord) const;

        [[nodiscard]] std::vector<TerrainTile*> getVisibleTiles(const math::Frustum& frustum);
        // VK-1336: non-mutating frustum query. Does NOT touch tile->isVisible or
        // lastVisibleTiles — safe to call alongside getVisibleTiles for additional
        // camera frustums (minimap / RTT) without stomping the main camera's flags.
        [[nodiscard]] std::vector<TerrainTile*> queryFrustumPure(const math::Frustum& frustum) const;
        [[nodiscard]] std::vector<TerrainTile*> getTilesInRange(const glm::vec3& center, float radius);
        [[nodiscard]] std::vector<TerrainTile*> getTilesInCone(const glm::vec3& apex, const glm::vec3& dir,
                                                               float halfAngle, float maxDist);

        void regenerateDirtyTiles(const glm::vec3& cameraPosition);

        [[nodiscard]] std::vector<TerrainTile*> getAllTiles();
        [[nodiscard]] std::vector<const TerrainTile*> getAllTiles() const;
        [[nodiscard]] size_t getTileCount() const { return tiles.size(); }
        [[nodiscard]] const TerrainTileConfig& getTileConfig() const { return config; }

        void createGrid(int32_t minX, int32_t minZ, int32_t maxX, int32_t maxZ,
                        ProgressCallback progress = nullptr);

        bool loadFromSerialized(const std::vector<TileLoadResult>& loadedTiles,
                                ProgressCallback progress = nullptr);

        bool loadMetadataOnly(const TerrainFileHeader& header,
                              const std::vector<TileIndexEntry>& index);

        void setFileCache(std::shared_ptr<TerrainFileCache> cache) { fileCache = std::move(cache); }
        [[nodiscard]] std::shared_ptr<TerrainFileCache> getFileCache() const { return fileCache; }
        [[nodiscard]] TerrainTileGenerator& getGenerator() { return *generator; }

        TerrainTile* addTile(const TileCoord& coord);
        TerrainTile* addTileFromFile(const TileCoord& coord);
        bool removeTile(const TileCoord& coord);
        void computeBounds(int32_t& minX, int32_t& minZ, int32_t& maxX, int32_t& maxZ) const;
        [[nodiscard]] bool hasTile(const TileCoord& coord) const { return tiles.find(coord) != tiles.end(); }

        template<typename F>
        void forEachTile(F&& func) const
        {
            for (const auto& [coord, tile] : tiles)
            {
                if (tile)
                    func(*tile);
            }
        }

        void initializeWeightMaps();
        [[nodiscard]] std::vector<TerrainTile*> getWeightMapDirtyTiles();

        // --- VK-1645: authoritative base vs derived heights ---

        [[nodiscard]] TerrainHeightLayerStore& getHeightLayers() { return *heightLayers; }
        [[nodiscard]] const TerrainHeightLayerStore& getHeightLayers() const { return *heightLayers; }

        // Recomposes every stale COVERED tile from its base plus the visible layer stack, then
        // normalizes seams across the stale set and its 4-neighbour ring.
        //
        // Strictly two phases. Interleaving compose and seam welding would let a later compose
        // overwrite an earlier seam write, leaving the boundary asymmetric.
        //
        // `budget` of 0 means "everything" -- use it for explicit user actions, where a partial
        // result would be visible. Non-zero paces residency-driven repair.
        // Returns the number of tiles whose derived heights were recomposed.
        uint32_t recomposeDirtyDerived(uint32_t budget = 0,
                                       std::vector<TileCoord>* outChanged = nullptr);

        // Derived-only seam welding, coverage-conditional (see the .cpp for the three cases).
        // Never writes an authoritative plane, never captures undo state, never dispatches.
        // `sorted` must be ordered -- the shared corner is written twice per call, so the result
        // depends on visit order.
        void normalizeDerivedSeams(const std::vector<TileCoord>& sorted);

        // ensureHeightsLoaded plus the VK-1645 rule: on a covered tile the bytes that just came
        // off disk are the uint16-quantized COMPOSITE, so they are a placeholder to be replaced
        // by an exact recompose from the in-RAM base.
        bool ensureTileHeights(TerrainTile& tile);

    private:
        // Composes one covered tile in place. Returns false when it is not covered, has no base,
        // or the base no longer matches the tile's resolution.
        bool recomposeTile(TerrainTile& tile);

        [[nodiscard]] TerrainTile* getOrCreateTile(const TileCoord& coord);

        void updateNeighborReferences(TerrainTile& tile);
        void updateAllNeighborReferences();
    };
#pragma warning(pop)

} // namespace terrain
