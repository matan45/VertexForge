#include "VegetationBrushServiceImpl.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/vegetation/VegetationBrushEvents.hpp"
#include "../../events/vegetation/GrassEvents.hpp"
#include "../../events/terrain/TerrainEvents.hpp"
#include "terrain/BrushSampler.hpp"
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace services
{
    VegetationBrushServiceImpl::~VegetationBrushServiceImpl()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        if (vegetationModeToken.isValid())
            dispatcher.unsubscribe(vegetationModeToken);
    }

    void VegetationBrushServiceImpl::registerEventHandlers()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        dispatcher.registerCommandHandler<events::vegetationBrush::SetVegetationBrushParamsCommand>(
            [this](const events::vegetationBrush::SetVegetationBrushParamsCommand& cmd)
            {
                currentParams = cmd.params;
            });

        dispatcher.registerCommandHandler<events::vegetationBrush::SetVegetationBrushTypeCommand>(
            [this](const events::vegetationBrush::SetVegetationBrushTypeCommand& cmd)
            {
                currentBrushType = cmd.type;
            });

        dispatcher.registerCommandHandler<events::vegetationBrush::ApplyVegetationBrushCommand>(
            [this](const events::vegetationBrush::ApplyVegetationBrushCommand& cmd)
            {
                applyBrush(cmd.worldPosition, cmd.deltaTime, cmd.isFirstApplication);
            });

        dispatcher.registerQueryHandler<events::vegetationBrush::GetVegetationBrushParamsQuery>(
            [this](const events::vegetationBrush::GetVegetationBrushParamsQuery&)
            {
                return currentParams;
            });

        dispatcher.registerQueryHandler<events::vegetationBrush::GetVegetationBrushTypeQuery>(
            [this](const events::vegetationBrush::GetVegetationBrushTypeQuery&)
            {
                return currentBrushType;
            });

        // Billboard palette management
        dispatcher.registerCommandHandler<events::vegetation::SetBillboardPaletteCommand>(
            [this](const events::vegetation::SetBillboardPaletteCommand& cmd)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                auto view = registry.view<components::GrassComponent>();
                entt::entity target = entt::null;
                for (auto entity : view) { target = entity; break; }
                if (target == entt::null)
                {
                    target = registry.create();
                    registry.emplace<components::GrassComponent>(target);
                }
                registry.get<components::GrassComponent>(target).billboardPalette = cmd.entries;
                if (billboardPaletteCb) billboardPaletteCb(cmd.entries, cmd.activeEntry);
            });

        dispatcher.registerQueryHandler<events::vegetation::GetBillboardPaletteQuery>(
            [](const events::vegetation::GetBillboardPaletteQuery&) -> std::vector<vegetation::BillboardPaletteEntry> {
                auto& registry = scene::EntityRegistry::getRegistry();
                auto view = registry.view<components::GrassComponent>();
                for (auto entity : view)
                    return view.get<components::GrassComponent>(entity).billboardPalette;
                return {};
            });

        vegetationModeToken = dispatcher.subscribe<events::vegetationBrush::VegetationBrushModeChangedNotification>(
            [this](const events::vegetationBrush::VegetationBrushModeChangedNotification& n)
            {
                vegetationModeActive = n.isActive;
                if (!vegetationModeActive)
                    hasLastPlacement = false;
            });
    }

    void VegetationBrushServiceImpl::applyBrush(const glm::vec3& worldPos,
                                                  float deltaTime, bool isFirstApplication)
    {
        if (!vegetationModeActive) return;

        if (isFirstApplication)
            hasLastPlacement = false;

        std::vector<vegetation::BillboardPaletteEntry> palette;
        try {
            palette = events::EventDispatcher::instance().query(
                events::vegetation::GetBillboardPaletteQuery{});
        } catch (...) { return; }

        if (currentBrushType == vegetation::VegetationBrushType::Paint)
        {
            if (hasLastPlacement)
            {
                float movedDist = glm::distance(glm::vec2(worldPos.x, worldPos.z),
                                                 glm::vec2(lastPlacementPos.x, lastPlacementPos.z));
                if (movedDist < currentParams.spacing * 0.5f)
                    return;
            }
            placeBillboards(worldPos, palette);
            lastPlacementPos = worldPos;
            hasLastPlacement = true;
        }
        else
        {
            eraseBillboards(worldPos);
        }
    }

    void VegetationBrushServiceImpl::placeBillboards(
        const glm::vec3& worldPos,
        const std::vector<vegetation::BillboardPaletteEntry>& palette)
    {
        // Build list of paint-enabled entries
        std::vector<uint32_t> enabledIndices;
        std::vector<float> weights;
        for (uint32_t i = 0; i < static_cast<uint32_t>(palette.size()); ++i)
        {
            if (palette[i].paintEnabled && !palette[i].texturePath.empty())
            {
                enabledIndices.push_back(i);
                weights.push_back(palette[i].weight);
            }
        }
        if (enabledIndices.empty()) return;

        std::discrete_distribution<uint32_t> paletteDist(weights.begin(), weights.end());

        // Calculate candidates
        float area = static_cast<float>(M_PI) * currentParams.radius * currentParams.radius;
        float spacingSq = std::max(currentParams.spacing * currentParams.spacing, 0.01f);
        uint32_t maxCandidates = std::clamp(
            static_cast<uint32_t>(currentParams.density * area / spacingSq), 1u, 100u);

        std::uniform_real_distribution<float> angleDist(0.0f, static_cast<float>(2.0 * M_PI));
        std::uniform_real_distribution<float> radiusDist(0.0f, 1.0f);
        std::uniform_real_distribution<float> jitterDist(-0.5f, 0.5f);
        std::uniform_real_distribution<float> unitDist(0.0f, 1.0f);

        // Group new instances by tile
        struct TileCoordHash {
            size_t operator()(const terrain::TileCoord& c) const {
                return std::hash<int>{}(c.x) ^ (std::hash<int>{}(c.z) << 16);
            }
        };
        struct TileCoordEqual {
            bool operator()(const terrain::TileCoord& a, const terrain::TileCoord& b) const {
                return a.x == b.x && a.z == b.z;
            }
        };
        std::unordered_map<terrain::TileCoord, std::vector<vegetation::BillboardInstance>,
                           TileCoordHash, TileCoordEqual> tileInstances;

        uint32_t placedCount = 0;

        for (uint32_t c = 0; c < maxCandidates; ++c)
        {
            // Uniform disk sampling
            float angle = angleDist(rng);
            float r = currentParams.radius * std::sqrt(radiusDist(rng));
            float candX = worldPos.x + r * std::cos(angle);
            float candZ = worldPos.z + r * std::sin(angle);

            // Jitter
            candX += jitterDist(rng) * currentParams.positionJitter * currentParams.spacing;
            candZ += jitterDist(rng) * currentParams.positionJitter * currentParams.spacing;

            // Terrain height query
            events::terrain::GetTerrainHeightAtQuery heightQuery;
            heightQuery.worldX = candX;
            heightQuery.worldZ = candZ;
            float candY = 0.0f;
            try {
                auto result = events::EventDispatcher::instance().query(heightQuery);
                if (!result.valid) continue;
                candY = result.height;
            } catch (...) { continue; }

            glm::vec3 candidatePos(candX, candY, candZ);
            terrain::TileCoord tileCoord = worldToTileCoord(candX, candZ);

            // Spacing check
            auto& grid = ensureSpatialGrid(tileCoord);
            if (grid.hasNeighborWithin(candidatePos, currentParams.spacing))
                continue;

            // Select palette entry
            uint32_t paletteIdx = enabledIndices[paletteDist(rng)];
            const auto& entry = palette[paletteIdx];

            // Random rotation + scale
            vegetation::BillboardInstance instance;
            instance.position = candidatePos;
            instance.rotation = unitDist(rng) * static_cast<float>(2.0 * M_PI);
            instance.scale = entry.scaleRange.x + unitDist(rng) * (entry.scaleRange.y - entry.scaleRange.x);
            instance.paletteEntryIndex = paletteIdx;
            instance.windPhase = unitDist(rng);

            tileInstances[tileCoord].push_back(instance);
            grid.insert(static_cast<uint32_t>(grid.queryRadius(candidatePos, 0.0f).size()), candidatePos);
            ++placedCount;
        }

        // Send instances to terrain tiles via events
        auto& dispatcher = events::EventDispatcher::instance();
        for (auto& [coord, instances] : tileInstances)
        {
            events::vegetation::AddBillboardInstancesToTileCommand cmd;
            cmd.tileX = coord.x;
            cmd.tileZ = coord.z;
            cmd.instances = std::move(instances);
            dispatcher.execute(cmd);
        }

        if (placedCount > 0)
        {
            events::vegetationBrush::VegetationBrushAppliedNotification notif;
            notif.position = worldPos;
            notif.placedCount = placedCount;
            dispatcher.publish(notif);
        }
    }

    void VegetationBrushServiceImpl::eraseBillboards(const glm::vec3& worldPos)
    {
        auto affectedTiles = terrain::BrushSampler::getAffectedTiles(
            glm::vec2(worldPos.x, worldPos.z), currentParams.radius, worldTileSize);

        auto& dispatcher = events::EventDispatcher::instance();
        uint32_t erasedCount = 0;

        for (const auto& coord : affectedTiles)
        {
            // Ensure spatial grid exists - rebuild from tile data if needed
            auto gridIt = spatialGrids.find(coord);
            if (gridIt == spatialGrids.end())
            {
                // Query tile instances to build grid
                events::vegetation::GetTileBillboardInstancesQuery query;
                query.tileX = coord.x;
                query.tileZ = coord.z;
                try {
                    auto instances = dispatcher.query(query);
                    if (instances.empty()) continue;
                    auto& grid = spatialGrids[coord];
                    grid.setCellSize(currentParams.spacing);
                    grid.rebuild(instances);
                    gridIt = spatialGrids.find(coord);
                } catch (...) { continue; }
            }

            auto entries = gridIt->second.queryRadius(worldPos, currentParams.radius);
            if (entries.empty()) continue;

            // Collect indices, sort descending, remove duplicates
            std::vector<uint32_t> toRemove;
            for (const auto& e : entries)
                toRemove.push_back(e.instanceIndex);
            std::sort(toRemove.begin(), toRemove.end(), std::greater<uint32_t>());
            toRemove.erase(std::unique(toRemove.begin(), toRemove.end()), toRemove.end());

            events::vegetation::RemoveBillboardInstancesFromTileCommand cmd;
            cmd.tileX = coord.x;
            cmd.tileZ = coord.z;
            cmd.indicesToRemove = toRemove;
            dispatcher.execute(cmd);

            erasedCount += static_cast<uint32_t>(toRemove.size());

            // Rebuild spatial grid (indices changed due to swap-and-pop)
            // Need to re-read tile data - for now just clear the grid
            // It will be rebuilt on next brush application
            spatialGrids.erase(gridIt);
        }

        if (erasedCount > 0)
        {
            events::vegetationBrush::VegetationBrushAppliedNotification notif;
            notif.position = worldPos;
            notif.erasedCount = erasedCount;
            dispatcher.publish(notif);
        }
    }

    terrain::TileCoord VegetationBrushServiceImpl::worldToTileCoord(float worldX, float worldZ) const
    {
        return {static_cast<int32_t>(std::floor(worldX / worldTileSize)),
                static_cast<int32_t>(std::floor(worldZ / worldTileSize))};
    }

    vegetation::VegetationSpatialGrid& VegetationBrushServiceImpl::ensureSpatialGrid(
        const terrain::TileCoord& coord)
    {
        auto it = spatialGrids.find(coord);
        if (it != spatialGrids.end())
            return it->second;

        auto& grid = spatialGrids[coord];
        grid.setCellSize(currentParams.spacing);
        return grid;
    }
}
