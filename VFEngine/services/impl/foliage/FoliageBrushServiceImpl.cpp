#include "FoliageBrushServiceImpl.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/TerrainComponents.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/foliage/FoliageBrushEvents.hpp"
#include "../../events/foliage/FoliageEvents.hpp"
#include "../../events/terrain/TerrainEvents.hpp"
#include "../../events/editor/UndoRedoEvents.hpp"
#include "../../data/FoliageUndoCommands.hpp"
#include "terrain/BrushSampler.hpp"
#include <glm/gtc/constants.hpp>
#include <cmath>
#include <algorithm>
#include <functional>
#include <memory>
#include <optional>

namespace services
{
    FoliageBrushServiceImpl::~FoliageBrushServiceImpl()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        if (foliageModeToken.isValid())
            dispatcher.unsubscribe(foliageModeToken);
    }

    void FoliageBrushServiceImpl::registerEventHandlers()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        dispatcher.registerCommandHandler<events::foliageBrush::SetFoliageBrushParamsCommand>(
            [this](const events::foliageBrush::SetFoliageBrushParamsCommand& cmd)
            {
                currentParams = cmd.params;
                currentParams.validate();
                // Notify the viewport ghost-ring overlay of the new radius/falloff.
                events::foliageBrush::FoliageBrushParamsChangedNotification notif;
                notif.params = currentParams;
                events::EventDispatcher::instance().publish(notif);
            });

        dispatcher.registerCommandHandler<events::foliageBrush::SetFoliageBrushModeCommand>(
            [this](const events::foliageBrush::SetFoliageBrushModeCommand& cmd)
            {
                // Close out any in-progress stroke before switching paint<->erase so a
                // single undo entry never straddles both operations.
                if (currentMode != cmd.mode)
                    finalizeStroke();
                currentMode = cmd.mode;
            });

        dispatcher.registerCommandHandler<events::foliageBrush::SetFoliageBrushSelectedEntryCommand>(
            [this](const events::foliageBrush::SetFoliageBrushSelectedEntryCommand& cmd)
            {
                selectedPaletteIndex = cmd.index;
            });

        dispatcher.registerCommandHandler<events::foliageBrush::ApplyFoliageBrushCommand>(
            [this](const events::foliageBrush::ApplyFoliageBrushCommand& cmd)
            {
                applyBrush(cmd.worldPosition, cmd.deltaTime, cmd.isFirstApplication);
            });

        dispatcher.registerCommandHandler<events::foliageBrush::FinalizeFoliageBrushCommand>(
            [this](const events::foliageBrush::FinalizeFoliageBrushCommand&)
            {
                finalizeStroke();
            });

        dispatcher.registerQueryHandler<events::foliageBrush::GetFoliageBrushParamsQuery>(
            [this](const events::foliageBrush::GetFoliageBrushParamsQuery&)
            {
                return currentParams;
            });

        dispatcher.registerQueryHandler<events::foliageBrush::GetFoliageBrushModeQuery>(
            [this](const events::foliageBrush::GetFoliageBrushModeQuery&)
            {
                return currentMode;
            });

        foliageModeToken = dispatcher.subscribe<events::foliageBrush::FoliageBrushModeChangedNotification>(
            [this](const events::foliageBrush::FoliageBrushModeChangedNotification& n)
            {
                foliageModeActive = n.isActive;
                if (!foliageModeActive)
                {
                    // Flush any pending stroke into an undo entry (no-op if none), then drop
                    // per-stroke placement state so a later activation (e.g. after a scene
                    // switch) never spacing-rejects against stale world positions.
                    finalizeStroke();
                    hasLastPlacement = false;
                    spatialGrids.clear();
                }
            });
    }

    void FoliageBrushServiceImpl::applyBrush(const glm::vec3& worldPos,
                                             float deltaTime, bool isFirstApplication)
    {
        if (!foliageModeActive) return;

        if (isFirstApplication)
        {
            hasLastPlacement = false;
            flowAccumulator = 0.0f;
            strokeBeforeSnapshots.clear();
        }

        // Query the actual tile size from terrain (drives worldToTileCoord + erase tiling).
        auto& registry = scene::EntityRegistry::getRegistry();
        auto terrainView = registry.view<components::TerrainComponent>();
        for (auto entity : terrainView)
        {
            worldTileSize = terrainView.get<components::TerrainComponent>(entity).worldTileSize;
            break;
        }

        if (currentMode == foliage::FoliageBrushMode::Erase)
        {
            eraseFoliage(worldPos);
            return;
        }

        // Single mode: one hero instance per click / per spacing move.
        if (currentParams.placementMode == foliage::FoliagePlacementMode::Single)
        {
            if (hasLastPlacement)
            {
                float movedDist = glm::distance(glm::vec2(worldPos.x, worldPos.z),
                                                glm::vec2(lastPlacementPos.x, lastPlacementPos.z));
                if (movedDist < currentParams.spacing)
                    return;
            }
            placeSingle(worldPos);
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
            placeFoliage(worldPos);
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
        placeFoliage(worldPos);
        lastPlacementPos = worldPos;
        hasLastPlacement = true;
    }

    void FoliageBrushServiceImpl::buildRules(
        const std::vector<foliage::FoliageType>& palette,
        std::vector<foliage::ScatterTypeRule>& rules,
        std::vector<uint32_t>& enabledIndices,
        bool restrictToSelected) const
    {
        rules.clear();
        enabledIndices.clear();
        rules.reserve(palette.size());

        for (const auto& t : palette)
        {
            foliage::ScatterTypeRule rule;
            rule.weight = t.weight;
            rule.scaleRange = t.scaleRange;
            rule.heightRange = t.heightRange;
            rule.rotationYRangeDeg = t.rotationYRange;
            rule.randomTiltDeg = t.randomTilt;
            rule.alignToNormal = t.alignToNormal;
            rule.minSlopeDeg = t.minSlopeDeg;
            rule.maxSlopeDeg = t.maxSlopeDeg;
            rule.altitudeRange = t.altitudeRange;
            rules.push_back(rule);
        }

        if (restrictToSelected && selectedPaletteIndex >= 0 &&
            selectedPaletteIndex < static_cast<int>(palette.size()))
        {
            const auto& t = palette[selectedPaletteIndex];
            if (t.paintEnabled && !t.meshPath.empty())
                enabledIndices.push_back(static_cast<uint32_t>(selectedPaletteIndex));
        }
        else
        {
            for (uint32_t i = 0; i < static_cast<uint32_t>(palette.size()); ++i)
            {
                if (palette[i].paintEnabled && !palette[i].meshPath.empty())
                    enabledIndices.push_back(i);
            }
        }
    }

    uint32_t FoliageBrushServiceImpl::generateAndDispatch(
        const glm::vec3& worldPos,
        const std::vector<foliage::ScatterTypeRule>& rules,
        const std::vector<uint32_t>& enabledIndices,
        const foliage::ScatterParams& params,
        const std::vector<foliage::FoliageType>& palette,
        bool useSpacing)
    {
        foliage::ScatterEnv env;
        env.heightAt = [](float x, float z) -> std::optional<float>
        {
            events::terrain::GetTerrainHeightAtQuery q;
            q.worldX = x;
            q.worldZ = z;
            try
            {
                auto r = events::EventDispatcher::instance().query(q);
                if (r.valid) return r.height;
            }
            catch (...) {}
            return std::nullopt;
        };
        env.normalAt = [this](float x, float z) { return sampleTerrainNormal(x, z); };
        if (useSpacing)
        {
            env.spacingReject = [this](const glm::vec3& pos)
            {
                return ensureSpatialGrid(worldToTileCoord(pos.x, pos.z))
                    .hasNeighborWithin(pos, currentParams.spacing);
            };
            // Placement grids are position-only (spacing checks); the instanceIndex is a
            // placeholder here — erase always rebuilds a tile's grid from real data.
            env.accept = [this](const glm::vec3& pos, uint16_t typeIndex)
            {
                ensureSpatialGrid(worldToTileCoord(pos.x, pos.z)).insert(0, pos, typeIndex);
            };
        }

        std::vector<foliage::ScatterCandidate> candidates;
        foliage::scatterFoliage(worldPos, rules, enabledIndices, params, env, rng,
                                glm::vec3(0.0f, 1.0f, 0.0f), candidates);
        if (candidates.empty()) return 0;

        // Group candidates by destination tile.
        TileInstanceMap tileInstances;
        for (const auto& cand : candidates)
        {
            foliage::FoliageInstance inst = candidateToInstance(cand, palette);
            tileInstances[worldToTileCoord(inst.position.x, inst.position.z)].push_back(inst);
        }

        auto& dispatcher = events::EventDispatcher::instance();
        uint32_t placedCount = 0;
        for (auto& [coord, instances] : tileInstances)
        {
            placedCount += static_cast<uint32_t>(instances.size());
            snapshotTileBefore(coord);
            events::foliage::AddFoliageInstancesToTileCommand cmd;
            cmd.tileX = coord.x;
            cmd.tileZ = coord.z;
            cmd.instances = std::move(instances);
            dispatcher.execute(cmd);
        }

        if (placedCount > 0)
        {
            events::foliageBrush::FoliageBrushAppliedNotification notif;
            notif.position = worldPos;
            notif.placedCount = placedCount;
            dispatcher.publish(notif);
        }
        return placedCount;
    }

    foliage::FoliageInstance FoliageBrushServiceImpl::candidateToInstance(
        const foliage::ScatterCandidate& cand,
        const std::vector<foliage::FoliageType>& palette) const
    {
        foliage::FoliageInstance inst;
        inst.position = cand.position;
        inst.rotationY = cand.rotationY;
        inst.scale = cand.scale;
        inst.typeIndex = cand.typeIndex;
        inst.normal = cand.normal;
        inst.windPhase = cand.windPhase;
        inst.seed = cand.seed;
        inst.tint = currentParams.tint;

        uint16_t flags = foliage::FoliageInstanceFlags::None;
        if (cand.tilted)
            flags |= foliage::FoliageInstanceFlags::Tilt;
        if (cand.typeIndex < palette.size())
        {
            const auto& t = palette[cand.typeIndex];
            if (t.collision)     flags |= foliage::FoliageInstanceFlags::Collider;
            if (t.navContribute) flags |= foliage::FoliageInstanceFlags::NavContribute;
        }
        inst.flags = flags;
        return inst;
    }

    void FoliageBrushServiceImpl::placeFoliage(const glm::vec3& worldPos)
    {
        std::vector<foliage::FoliageType> palette;
        try {
            palette = events::EventDispatcher::instance().query(
                events::foliage::GetFoliagePaletteQuery{});
        } catch (...) { return; }

        std::vector<foliage::ScatterTypeRule> rules;
        std::vector<uint32_t> enabledIndices;
        buildRules(palette, rules, enabledIndices, /*restrictToSelected*/false);
        if (enabledIndices.empty()) return;

        foliage::ScatterParams params;
        params.radius = currentParams.radius;
        params.spacing = currentParams.spacing;
        params.positionJitter = currentParams.positionJitter;
        params.falloff = currentParams.falloff;
        params.applyFalloff = true;
        params.perCandidateNormal = true;

        const float area = glm::pi<float>() * currentParams.radius * currentParams.radius;
        const float spacingSq = std::max(currentParams.spacing * currentParams.spacing, 0.01f);
        params.maxCandidates = std::clamp(
            static_cast<uint32_t>(currentParams.density * area / spacingSq), 1u, 4096u);

        generateAndDispatch(worldPos, rules, enabledIndices, params, palette, /*useSpacing*/true);
    }

    void FoliageBrushServiceImpl::placeSingle(const glm::vec3& worldPos)
    {
        std::vector<foliage::FoliageType> palette;
        try {
            palette = events::EventDispatcher::instance().query(
                events::foliage::GetFoliagePaletteQuery{});
        } catch (...) { return; }

        std::vector<foliage::ScatterTypeRule> rules;
        std::vector<uint32_t> enabledIndices;
        buildRules(palette, rules, enabledIndices, /*restrictToSelected*/true);
        if (enabledIndices.empty()) return;

        // Single hero instance seated exactly at the cursor: zero radius/jitter so the sole
        // candidate lands at worldPos, no falloff thinning, and no spacing gate.
        foliage::ScatterParams params;
        params.radius = 0.0f;
        params.spacing = currentParams.spacing;
        params.positionJitter = 0.0f;
        params.falloff = currentParams.falloff;
        params.applyFalloff = false;
        params.perCandidateNormal = true;
        params.maxCandidates = 1;

        generateAndDispatch(worldPos, rules, enabledIndices, params, palette, /*useSpacing*/false);
    }

    void FoliageBrushServiceImpl::eraseFoliage(const glm::vec3& worldPos)
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

            // Collect tile-vector indices, honoring erase-selected-type; sort descending +
            // dedupe so RemoveFoliageInstancesFromTileCommand's swap-and-pop stays valid.
            std::vector<uint32_t> toRemove;
            for (const auto& e : entries)
            {
                if (currentParams.eraseSelectedTypeOnly && selectedPaletteIndex >= 0 &&
                    e.typeIndex != static_cast<uint16_t>(selectedPaletteIndex))
                    continue;
                toRemove.push_back(e.instanceIndex);
            }
            if (toRemove.empty()) continue;

            std::sort(toRemove.begin(), toRemove.end(), std::greater<uint32_t>());
            toRemove.erase(std::unique(toRemove.begin(), toRemove.end()), toRemove.end());

            snapshotTileBefore(coord);
            events::foliage::RemoveFoliageInstancesFromTileCommand cmd;
            cmd.tileX = coord.x;
            cmd.tileZ = coord.z;
            cmd.indicesToRemove = toRemove;
            dispatcher.execute(cmd);

            erasedCount += static_cast<uint32_t>(toRemove.size());

            // Indices shifted (swap-and-pop) — drop the grid so it rebuilds next stroke.
            spatialGrids.erase(coord);
        }

        if (erasedCount > 0)
        {
            events::foliageBrush::FoliageBrushAppliedNotification notif;
            notif.position = worldPos;
            notif.erasedCount = erasedCount;
            dispatcher.publish(notif);
        }
    }

    void FoliageBrushServiceImpl::snapshotTileBefore(const terrain::TileCoord& coord)
    {
        if (strokeBeforeSnapshots.find(coord) != strokeBeforeSnapshots.end())
            return; // already captured this stroke

        events::foliage::GetTileFoliageInstancesQuery query;
        query.tileX = coord.x;
        query.tileZ = coord.z;
        try {
            strokeBeforeSnapshots[coord] = events::EventDispatcher::instance().query(query);
        } catch (...) {
            strokeBeforeSnapshots[coord] = {};
        }
    }

    void FoliageBrushServiceImpl::finalizeStroke()
    {
        if (strokeBeforeSnapshots.empty())
            return;

        auto& dispatcher = events::EventDispatcher::instance();
        auto undoCmd = std::make_shared<FoliageTileSnapshotUndoCommand>("Foliage Brush");

        for (auto& [coord, before] : strokeBeforeSnapshots)
        {
            events::foliage::GetTileFoliageInstancesQuery query;
            query.tileX = coord.x;
            query.tileZ = coord.z;
            std::vector<foliage::FoliageInstance> after;
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

    glm::vec3 FoliageBrushServiceImpl::sampleTerrainNormal(float worldX, float worldZ) const
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

    terrain::TileCoord FoliageBrushServiceImpl::worldToTileCoord(float worldX, float worldZ) const
    {
        return {static_cast<int32_t>(std::floor(worldX / worldTileSize)),
                static_cast<int32_t>(std::floor(worldZ / worldTileSize))};
    }

    foliage::FoliageSpatialGrid& FoliageBrushServiceImpl::ensureSpatialGrid(
        const terrain::TileCoord& coord)
    {
        auto it = spatialGrids.find(coord);
        if (it != spatialGrids.end())
            return it->second;

        auto& grid = spatialGrids[coord];
        grid.setCellSize(currentParams.spacing);
        return grid;
    }

    bool FoliageBrushServiceImpl::ensureSpatialGridForTile(const terrain::TileCoord& coord)
    {
        // Always rebuild from authoritative tile data so instanceIndex maps 1:1 to the
        // tile's FoliageInstance vector (FoliageSpatialGrid has no rebuild(); placement
        // grids hold position-only placeholders that must never drive erase).
        spatialGrids.erase(coord);

        events::foliage::GetTileFoliageInstancesQuery query;
        query.tileX = coord.x;
        query.tileZ = coord.z;
        std::vector<foliage::FoliageInstance> instances;
        try {
            instances = events::EventDispatcher::instance().query(query);
        } catch (...) { return false; }
        if (instances.empty()) return false;

        auto& grid = spatialGrids[coord];
        grid.setCellSize(currentParams.spacing);
        for (uint32_t i = 0; i < static_cast<uint32_t>(instances.size()); ++i)
            grid.insert(i, instances[i].position, instances[i].typeIndex);
        return true;
    }
}
