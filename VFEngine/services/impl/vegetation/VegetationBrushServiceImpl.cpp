#include "VegetationBrushServiceImpl.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "components/TerrainComponents.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/vegetation/VegetationBrushEvents.hpp"
#include "../../events/vegetation/GrassEvents.hpp"
#include "../../events/terrain/TerrainEvents.hpp"
#include "../../events/scene/ScenePersistenceEvents.hpp"
#include "../../events/editor/UndoRedoEvents.hpp"
#include "../../data/VegetationUndoCommands.hpp"
#include "terrain/BrushSampler.hpp"
#include "terrain/BrushFalloff.hpp"
#include "terrain/ValueNoise.hpp"
#include <cmath>
#include <algorithm>
#include <memory>

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
        if (sceneLoadedToken.isValid())
            dispatcher.unsubscribe(sceneLoadedToken);
    }

    void VegetationBrushServiceImpl::registerEventHandlers()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        dispatcher.registerCommandHandler<events::vegetationBrush::SetVegetationBrushParamsCommand>(
            [this](const events::vegetationBrush::SetVegetationBrushParamsCommand& cmd)
            {
                currentParams = cmd.params;
                // Notify the viewport ghost-ring overlay of the new radius/falloff.
                events::vegetationBrush::VegetationBrushParamsChangedNotification notif;
                notif.params = currentParams;
                events::EventDispatcher::instance().publish(notif);
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

        dispatcher.registerCommandHandler<events::vegetationBrush::FinalizeVegetationBrushCommand>(
            [this](const events::vegetationBrush::FinalizeVegetationBrushCommand&)
            {
                finalizeStroke();
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

                // Find entity with GrassComponent, or add to terrain entity
                auto grassView = registry.view<components::GrassComponent>();
                entt::entity target = entt::null;
                for (auto entity : grassView) { target = entity; break; }

                if (target == entt::null)
                {
                    // Add GrassComponent to the terrain entity
                    auto terrainView = registry.view<components::TerrainComponent>();
                    for (auto entity : terrainView) { target = entity; break; }

                    if (target != entt::null)
                        registry.emplace<components::GrassComponent>(target);
                }

                if (target != entt::null)
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

        // Auto-push billboard palette to renderer when scene loads
        sceneLoadedToken = dispatcher.subscribe<events::scene::SceneLoadedNotification>(
            [this](const events::scene::SceneLoadedNotification&)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                auto view = registry.view<components::GrassComponent>();
                for (auto entity : view)
                {
                    auto& palette = view.get<components::GrassComponent>(entity).billboardPalette;
                    if (!palette.empty() && billboardPaletteCb)
                        billboardPaletteCb(palette, -1);
                    break;
                }
            });
    }

    void VegetationBrushServiceImpl::applyBrush(const glm::vec3& worldPos,
                                                  float deltaTime, bool isFirstApplication)
    {
        if (!vegetationModeActive) return;

        if (isFirstApplication)
        {
            hasLastPlacement = false;
            flowAccumulator = 0.0f;
            strokeBeforeSnapshots.clear();
        }

        // Query actual tile size from terrain
        auto& registry = scene::EntityRegistry::getRegistry();
        auto terrainView = registry.view<components::TerrainComponent>();
        for (auto entity : terrainView)
        {
            worldTileSize = terrainView.get<components::TerrainComponent>(entity).worldTileSize;
            break;
        }

        std::vector<vegetation::BillboardPaletteEntry> palette;
        try {
            palette = events::EventDispatcher::instance().query(
                events::vegetation::GetBillboardPaletteQuery{});
        } catch (...) { return; }

        if (currentBrushType == vegetation::VegetationBrushType::Erase)
        {
            eraseBillboards(worldPos);
            return;
        }

        // Single mode: one hero instance per click / per spacing move.
        if (currentParams.placementMode == vegetation::VegetationPlacementMode::Single)
        {
            if (hasLastPlacement)
            {
                float movedDist = glm::distance(glm::vec2(worldPos.x, worldPos.z),
                                                 glm::vec2(lastPlacementPos.x, lastPlacementPos.z));
                if (movedDist < currentParams.spacing)
                    return;
            }
            placeSingle(worldPos, palette);
            lastPlacementPos = worldPos;
            hasLastPlacement = true;
            return;
        }

        // Airbrush flow: paint continuously while held, gated by flowRate (sprays/sec),
        // so a stationary cursor keeps accumulating instead of stopping.
        if (currentParams.flowRate > 0.0f)
        {
            flowAccumulator += currentParams.flowRate * deltaTime;
            if (flowAccumulator < 1.0f)
                return;
            flowAccumulator -= std::floor(flowAccumulator);
            placeBillboards(worldPos, palette);
            lastPlacementPos = worldPos;
            hasLastPlacement = true;
            return;
        }

        // Default spray: throttle by cursor movement to avoid over-spamming one spot.
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

    glm::vec3 VegetationBrushServiceImpl::sampleTerrainNormal(float worldX, float worldZ) const
    {
        const float eps = 0.5f;
        auto sample = [](float x, float z, float fallback) -> float {
            events::terrain::GetTerrainHeightAtQuery q;
            q.worldX = x; q.worldZ = z;
            try {
                auto r = events::EventDispatcher::instance().query(q);
                return r.valid ? r.height : fallback;
            } catch (...) { return fallback; }
        };
        float hC = sample(worldX, worldZ, 0.0f);
        float hL = sample(worldX - eps, worldZ, hC);
        float hR = sample(worldX + eps, worldZ, hC);
        float hD = sample(worldX, worldZ - eps, hC);
        float hU = sample(worldX, worldZ + eps, hC);
        glm::vec3 n(hL - hR, 2.0f * eps, hD - hU);
        return glm::normalize(n);
    }

    bool VegetationBrushServiceImpl::passesMasks(float candY, const glm::vec3& normal,
                                                 float candX, float candZ) const
    {
        if (currentParams.useSlopeMask)
        {
            if (normal.y < currentParams.slopeMinCos || normal.y > currentParams.slopeMaxCos)
                return false;
        }
        if (currentParams.useHeightMask)
        {
            if (candY < currentParams.heightMin || candY > currentParams.heightMax)
                return false;
        }
        if (currentParams.useNoiseMask)
        {
            float n = terrain::valueNoise2D(candX * currentParams.noiseFrequency,
                                            candZ * currentParams.noiseFrequency,
                                            currentParams.noiseSeed);
            if (n < currentParams.noiseThreshold)
                return false;
        }
        return true;
    }

    void VegetationBrushServiceImpl::generateAndPlaceCandidates(
        const glm::vec3& worldPos, PlacementContext& ctx)
    {
        const auto& enabledIndices = ctx.enabledIndices;
        const auto& palette = ctx.palette;
        auto& paletteDist = ctx.paletteDist;
        uint32_t maxCandidates = ctx.maxCandidates;
        auto& tileInstances = ctx.tileInstances;
        auto& placedCount = ctx.placedCount;
        std::uniform_real_distribution<float> angleDist(0.0f, static_cast<float>(2.0 * M_PI));
        std::uniform_real_distribution<float> radiusDist(0.0f, 1.0f);
        std::uniform_real_distribution<float> jitterDist(-0.5f, 0.5f);
        std::uniform_real_distribution<float> unitDist(0.0f, 1.0f);

        for (uint32_t c = 0; c < maxCandidates; ++c)
        {
            // Uniform disk sampling
            float angle = angleDist(rng);
            float r = currentParams.radius * std::sqrt(radiusDist(rng));

            // Density falloff: thin out placement toward the brush edge per the
            // selected curve (Constant = no thinning). Done before the terrain
            // height query so rejected candidates are cheap.
            float normDist = std::clamp(r / std::max(currentParams.radius, 0.001f), 0.0f, 1.0f);
            if (unitDist(rng) > terrain::applyFalloff(normDist, currentParams.falloff))
                continue;

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

            // Layer-aware avoidance: keep different layers from interleaving.
            if (currentParams.avoidOtherLayers &&
                grid.hasNeighborOfOtherLayer(candidatePos, currentParams.layerAvoidRadius, paletteIdx))
                continue;

            // Slope/height/noise masks (compute normal only when needed)
            glm::vec3 normal(0.0f, 1.0f, 0.0f);
            if (currentParams.useSlopeMask || currentParams.alignToNormal)
                normal = sampleTerrainNormal(candX, candZ);
            if (!passesMasks(candY, normal, candX, candZ))
                continue;

            vegetation::BillboardInstance instance = buildInstance(
                candidatePos, normal, paletteIdx, entry, unitDist);

            tileInstances[tileCoord].push_back(instance);
            grid.insert(placedCount, candidatePos, paletteIdx);
            ++placedCount;
        }
    }

    vegetation::BillboardInstance VegetationBrushServiceImpl::buildInstance(
        const glm::vec3& position, const glm::vec3& normal, uint32_t paletteIdx,
        const vegetation::BillboardPaletteEntry& entry,
        std::uniform_real_distribution<float>& unitDist)
    {
        vegetation::BillboardInstance instance;
        instance.position = position;
        instance.rotation = unitDist(rng) * static_cast<float>(2.0 * M_PI);
        instance.scale = entry.scaleRange.x + unitDist(rng) * (entry.scaleRange.y - entry.scaleRange.x);
        instance.heightScale = entry.heightRange.x +
            unitDist(rng) * (entry.heightRange.y - entry.heightRange.x);
        // tint: 1 +/- tintJitter (per instance brightness variation)
        instance.tint = 1.0f + (unitDist(rng) * 2.0f - 1.0f) * entry.tintJitter;
        instance.paletteEntryIndex = paletteIdx;
        instance.windPhase = unitDist(rng);
        instance.normal = currentParams.alignToNormal ? normal : glm::vec3(0.0f, 1.0f, 0.0f);
        return instance;
    }

    void VegetationBrushServiceImpl::placeSingle(
        const glm::vec3& worldPos,
        const std::vector<vegetation::BillboardPaletteEntry>& palette)
    {
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
        std::uniform_real_distribution<float> unitDist(0.0f, 1.0f);

        uint32_t paletteIdx = enabledIndices[paletteDist(rng)];
        glm::vec3 normal(0.0f, 1.0f, 0.0f);
        if (currentParams.useSlopeMask || currentParams.alignToNormal)
            normal = sampleTerrainNormal(worldPos.x, worldPos.z);
        if (!passesMasks(worldPos.y, normal, worldPos.x, worldPos.z))
            return;

        vegetation::BillboardInstance instance = buildInstance(
            worldPos, normal, paletteIdx, palette[paletteIdx], unitDist);

        terrain::TileCoord tileCoord = worldToTileCoord(worldPos.x, worldPos.z);
        snapshotTileBefore(tileCoord);
        events::vegetation::AddBillboardInstancesToTileCommand cmd;
        cmd.tileX = tileCoord.x;
        cmd.tileZ = tileCoord.z;
        cmd.instances = {instance};
        events::EventDispatcher::instance().execute(cmd);

        events::vegetationBrush::VegetationBrushAppliedNotification notif;
        notif.position = worldPos;
        notif.placedCount = 1;
        events::EventDispatcher::instance().publish(notif);
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

        // Calculate candidates. The cap only bounds per-stroke candidate
        // iterations (each does a terrain height + spacing query); actual
        // placement stays bounded by spacing + falloff rejection. Large brushes
        // need far more than the old 100 to fill, so the cap is generous.
        static constexpr uint32_t MAX_CANDIDATES_PER_STROKE = 4096;
        float area = static_cast<float>(M_PI) * currentParams.radius * currentParams.radius;
        float spacingSq = std::max(currentParams.spacing * currentParams.spacing, 0.01f);
        uint32_t maxCandidates = std::clamp(
            static_cast<uint32_t>(currentParams.density * area / spacingSq),
            1u, MAX_CANDIDATES_PER_STROKE);

        TileInstanceMap tileInstances;
        uint32_t placedCount = 0;

        PlacementContext ctx{enabledIndices, palette, paletteDist,
                             maxCandidates, tileInstances, placedCount};
        generateAndPlaceCandidates(worldPos, ctx);

        // Send instances to terrain tiles via events
        auto& dispatcher = events::EventDispatcher::instance();
        for (auto& [coord, instances] : tileInstances)
        {
            snapshotTileBefore(coord);
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

    bool VegetationBrushServiceImpl::ensureSpatialGridForTile(const terrain::TileCoord& coord)
    {
        auto gridIt = spatialGrids.find(coord);
        if (gridIt != spatialGrids.end())
            return true;

        events::vegetation::GetTileBillboardInstancesQuery query;
        query.tileX = coord.x;
        query.tileZ = coord.z;
        try {
            auto instances = events::EventDispatcher::instance().query(query);
            if (instances.empty()) return false;
            auto& grid = spatialGrids[coord];
            grid.setCellSize(currentParams.spacing);
            grid.rebuild(instances);
            return true;
        } catch (...) { return false; }
    }

    void VegetationBrushServiceImpl::eraseBillboards(const glm::vec3& worldPos)
    {
        auto affectedTiles = terrain::BrushSampler::getAffectedTiles(
            glm::vec2(worldPos.x, worldPos.z), currentParams.radius, worldTileSize);

        auto& dispatcher = events::EventDispatcher::instance();
        uint32_t erasedCount = 0;

        for (const auto& coord : affectedTiles)
        {
            if (!ensureSpatialGridForTile(coord))
                continue;

            auto gridIt = spatialGrids.find(coord);
            auto entries = gridIt->second.queryRadius(worldPos, currentParams.radius);
            if (entries.empty()) continue;

            // Collect indices, sort descending, remove duplicates
            std::vector<uint32_t> toRemove;
            for (const auto& e : entries)
                toRemove.push_back(e.instanceIndex);
            std::sort(toRemove.begin(), toRemove.end(), std::greater<uint32_t>());
            toRemove.erase(std::unique(toRemove.begin(), toRemove.end()), toRemove.end());

            snapshotTileBefore(coord);
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

    void VegetationBrushServiceImpl::snapshotTileBefore(const terrain::TileCoord& coord)
    {
        if (strokeBeforeSnapshots.find(coord) != strokeBeforeSnapshots.end())
            return; // already captured this stroke

        events::vegetation::GetTileBillboardInstancesQuery query;
        query.tileX = coord.x;
        query.tileZ = coord.z;
        try {
            strokeBeforeSnapshots[coord] = events::EventDispatcher::instance().query(query);
        } catch (...) {
            strokeBeforeSnapshots[coord] = {};
        }
    }

    void VegetationBrushServiceImpl::finalizeStroke()
    {
        if (strokeBeforeSnapshots.empty())
            return;

        auto& dispatcher = events::EventDispatcher::instance();
        auto undoCmd = std::make_shared<VegetationTileSnapshotUndoCommand>("Vegetation Brush");

        for (auto& [coord, before] : strokeBeforeSnapshots)
        {
            events::vegetation::GetTileBillboardInstancesQuery query;
            query.tileX = coord.x;
            query.tileZ = coord.z;
            std::vector<vegetation::BillboardInstance> after;
            try { after = dispatcher.query(query); } catch (...) {}
            undoCmd->addTile(coord.x, coord.z, before, std::move(after));
        }
        strokeBeforeSnapshots.clear();

        if (undoCmd->hasChanges())
        {
            events::undoredo::PushUndoableCommand pushCmd;
            pushCmd.command = undoCmd;
            dispatcher.execute(pushCmd);
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
