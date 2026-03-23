#pragma once
#include "../../interfaces/vegetation/IVegetationBrushService.hpp"
#include "../../events/EventTypes.hpp"
#include "../../../utilities/vegetation/VegetationTypes.hpp"
#include "../../../utilities/vegetation/VegetationSpatialGrid.hpp"
#include "../../../utilities/terrain/TerrainTypes.hpp"
#include <random>
#include <unordered_map>
#include <functional>

namespace services
{
    using BillboardPaletteCallback = std::function<void(const std::vector<vegetation::BillboardPaletteEntry>&, int32_t activeEntry)>;

    class VegetationBrushServiceImpl : public IVegetationBrushService
    {
    private:
        vegetation::VegetationBrushParams currentParams;
        vegetation::VegetationBrushType currentBrushType = vegetation::VegetationBrushType::Paint;
        bool vegetationModeActive = false;

        // Spatial grids per tile for erase queries and spacing checks
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
        std::unordered_map<terrain::TileCoord, vegetation::VegetationSpatialGrid, TileCoordHash, TileCoordEqual> spatialGrids;

        // Placement state
        std::mt19937 rng{std::random_device{}()};
        glm::vec3 lastPlacementPos{0.0f};
        bool hasLastPlacement = false;
        float worldTileSize = 32.0f;

        BillboardPaletteCallback billboardPaletteCb;
        ::events::SubscriptionToken vegetationModeToken;
        ::events::SubscriptionToken sceneLoadedToken;

    public:
        VegetationBrushServiceImpl() = default;
        ~VegetationBrushServiceImpl() override;

        void registerEventHandlers() override;
        void setBillboardPaletteCallback(BillboardPaletteCallback cb) { billboardPaletteCb = std::move(cb); }
        void setWorldTileSize(float size) { worldTileSize = size; }

    private:
        void applyBrush(const glm::vec3& worldPos, float deltaTime, bool isFirstApplication);
        void placeBillboards(const glm::vec3& worldPos,
                             const std::vector<vegetation::BillboardPaletteEntry>& palette);
        void eraseBillboards(const glm::vec3& worldPos);

        terrain::TileCoord worldToTileCoord(float worldX, float worldZ) const;
        vegetation::VegetationSpatialGrid& ensureSpatialGrid(const terrain::TileCoord& coord);
        bool ensureSpatialGridForTile(const terrain::TileCoord& coord);

        using TileInstanceMap = std::unordered_map<terrain::TileCoord,
            std::vector<vegetation::BillboardInstance>, TileCoordHash, TileCoordEqual>;

        struct PlacementContext
        {
            const std::vector<uint32_t>& enabledIndices;
            const std::vector<vegetation::BillboardPaletteEntry>& palette;
            std::discrete_distribution<uint32_t>& paletteDist;
            uint32_t maxCandidates;
            TileInstanceMap& tileInstances;
            uint32_t& placedCount;
        };

        void generateAndPlaceCandidates(const glm::vec3& worldPos, PlacementContext& ctx);
    };
}
