#pragma once
// VK-1575 (Phase 4): foliage brush service. Structural mirror of
// vegetation/VegetationBrushServiceImpl, but the candidate-generation inner loop is
// delegated to the shared, Vulkan-free foliage::scatterFoliage core instead of an inline
// generateAndPlaceCandidates. Per-tile FoliageInstance storage + the FoliageType palette
// are owned by TerrainService and reached through the events::foliage command/query set.
#include "../../interfaces/foliage/IFoliageBrushService.hpp"
#include "../../events/EventTypes.hpp"
#include "../../../utilities/foliage/FoliageBrushTypes.hpp"
#include "../../../utilities/foliage/FoliageTypes.hpp"
#include "../../../utilities/foliage/FoliageSpatialGrid.hpp"
#include "../../../utilities/foliage/FoliageScatter.hpp"
#include "../../../utilities/terrain/TerrainTypes.hpp"
#include <glm/glm.hpp>
#include <random>
#include <unordered_map>
#include <vector>

namespace services
{
    class FoliageBrushServiceImpl : public IFoliageBrushService
    {
    private:
        foliage::FoliageBrushParams currentParams;
        foliage::FoliageBrushMode currentMode = foliage::FoliageBrushMode::Paint;
        bool foliageModeActive = false;
        // -1 = all paint-enabled entries (weighted). >=0 restricts Single-mode placement
        // and erase-selected-type to that palette index.
        int selectedPaletteIndex = -1;

        // Spatial grids per tile for erase queries and spacing checks.
        struct TileCoordHash
        {
            size_t operator()(const terrain::TileCoord& c) const
            {
                return std::hash<int>{}(c.x) ^ (std::hash<int>{}(c.z) << 16);
            }
        };
        struct TileCoordEqual
        {
            bool operator()(const terrain::TileCoord& a, const terrain::TileCoord& b) const
            {
                return a.x == b.x && a.z == b.z;
            }
        };
        std::unordered_map<terrain::TileCoord, foliage::FoliageSpatialGrid, TileCoordHash, TileCoordEqual> spatialGrids;

        // Per-stroke "before" snapshots of touched tiles, for one undo entry per stroke.
        using TileSnapshotMap = std::unordered_map<terrain::TileCoord,
            std::vector<foliage::FoliageInstance>, TileCoordHash, TileCoordEqual>;
        TileSnapshotMap strokeBeforeSnapshots;

        using TileInstanceMap = std::unordered_map<terrain::TileCoord,
            std::vector<foliage::FoliageInstance>, TileCoordHash, TileCoordEqual>;

        // Placement state
        std::mt19937 rng{std::random_device{}()};
        glm::vec3 lastPlacementPos{0.0f};
        bool hasLastPlacement = false;
        float worldTileSize = 32.0f;
        float flowAccumulator = 0.0f; // Airbrush flow timing (instances accumulate over time)

        ::events::SubscriptionToken foliageModeToken;

    public:
        FoliageBrushServiceImpl() = default;
        ~FoliageBrushServiceImpl() override;

        void registerEventHandlers() override;

    private:
        void applyBrush(const glm::vec3& worldPos, float deltaTime, bool isFirstApplication);
        void placeFoliage(const glm::vec3& worldPos);
        void placeSingle(const glm::vec3& worldPos);
        void eraseFoliage(const glm::vec3& worldPos);

        // Build a palette-index-aligned rule set + the enabled-index list. When
        // restrictToSelected is true and a valid selectedPaletteIndex is set, only that
        // entry is enabled; otherwise every paint-enabled, non-empty-mesh entry is.
        void buildRules(const std::vector<foliage::FoliageType>& palette,
                        std::vector<foliage::ScatterTypeRule>& rules,
                        std::vector<uint32_t>& enabledIndices,
                        bool restrictToSelected) const;

        // Run the scatter core with `params`, map candidates -> FoliageInstance, group by
        // destination tile, snapshot + dispatch AddFoliageInstancesToTileCommand per tile,
        // and publish FoliageBrushAppliedNotification. Returns the number of instances placed.
        uint32_t generateAndDispatch(const glm::vec3& worldPos,
                                     const std::vector<foliage::ScatterTypeRule>& rules,
                                     const std::vector<uint32_t>& enabledIndices,
                                     const foliage::ScatterParams& params,
                                     const std::vector<foliage::FoliageType>& palette,
                                     bool useSpacing);

        foliage::FoliageInstance candidateToInstance(
            const foliage::ScatterCandidate& cand,
            const std::vector<foliage::FoliageType>& palette) const;

        // Undo support: capture a tile's instances before the stroke mutates it,
        // then build+push one undo command when the stroke finalizes.
        void snapshotTileBefore(const terrain::TileCoord& coord);
        void finalizeStroke();

        // Estimate the terrain surface normal at (worldX, worldZ) via finite differences
        // over the existing GetTerrainHeightAtQuery.
        glm::vec3 sampleTerrainNormal(float worldX, float worldZ) const;

        terrain::TileCoord worldToTileCoord(float worldX, float worldZ) const;
        // Position-only spacing grid used while placing candidates within a stroke.
        foliage::FoliageSpatialGrid& ensureSpatialGrid(const terrain::TileCoord& coord);
        // Rebuild a tile's grid from authoritative instance data (correct instanceIndex ->
        // tile-vector mapping) for erase. Returns false when the tile has no instances.
        bool ensureSpatialGridForTile(const terrain::TileCoord& coord);
    };
}
