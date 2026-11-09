#include "TerrainService.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "terrain/TerrainGrid.hpp"
#include "terrain/TerrainTile.hpp"
#include "terrain/TerrainTypes.hpp"
#include "terrain/TerrainMaterialTypes.hpp"
#include "terrain/BrushSampler.hpp"
#include "terrain/BrushStroke.hpp"
#include "terrain/HeightmapLoader.hpp"
#include "terrain/TileHeightSampler.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/terrain/BrushEvents.hpp"
#include "../../events/terrain/TerrainAuthoringEvents.hpp"
#include "../../events/terrain/TerrainStrokeEvents.hpp"
#include "print/Log.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <memory>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

// VK-1653 terrain authoring strokes (MCP P4): sculpt, layer paint and heightmap apply.
//
// Each command is a complete authoring edit in ONE synchronous main-thread call: validate, check the
// work budget, then run the same mode-free per-dab cores the editor brush uses (applySculptDab /
// applyLayerPaintDab in TerrainBrushOps.cpp) inside one VK-1615 stroke, so the call leaves exactly
// zero or one undo entry behind. Every refusal happens before the first mutation.
//
// What differs from a held editor brush is only the framing: deltaTime is 1 instead of a frame
// time, the per-dab strength is normalised by the stroke's centreline overlap (BrushStroke.hpp) so
// `amount` / `strength` describe the STROKE rather than one dab, and colliders are rebuilt once at
// finalize instead of after every dab.

namespace services
{
    namespace
    {
        namespace authoring = ::events::terrainAuthoring;

        // The MCP layer bounds agent coordinates to |v| <= 1e6 already. The engine repeats the bound
        // because BrushSampler turns (x +- radius) / tileSize into int32 tile coords, and a float to
        // int conversion out of range is undefined behaviour rather than an error.
        constexpr float MAX_ABS_COORDINATE = 1.0e6f;

        constexpr const char* APPLY_HEIGHTMAP_LABEL = "Apply Heightmap";

        TerrainStrokeResult refuse(TerrainStrokeStatus status, std::string message,
                                   EntityHandle terrainEntity = EntityHandle{})
        {
            TerrainStrokeResult result;
            result.status = status;
            result.message = std::move(message);
            result.terrainEntity = terrainEntity;
            return result;
        }

        // Shared by sculpt and paint. Returns the empty string when the stroke shape is valid.
        std::string validateStroke(const std::vector<glm::vec2>& points, float radius, float spacing,
                                   terrain::BrushFalloff falloff, terrain::BrushShape shape)
        {
            if (points.empty())
                return "a stroke needs at least one point";

            for (size_t i = 0; i < points.size(); ++i)
            {
                const glm::vec2& point = points[i];
                if (!std::isfinite(point.x) || !std::isfinite(point.y)
                    || std::abs(point.x) > MAX_ABS_COORDINATE || std::abs(point.y) > MAX_ABS_COORDINATE)
                {
                    return std::format("point {} ({}, {}) is not a finite coordinate within +/-{}",
                                       i, point.x, point.y, MAX_ABS_COORDINATE);
                }
            }

            // Written as !(in range) so NaN is rejected too.
            if (!(radius >= authoring::MIN_BRUSH_RADIUS && radius <= authoring::MAX_BRUSH_RADIUS))
            {
                return std::format("radius {} is outside [{}, {}] metres", radius,
                                   authoring::MIN_BRUSH_RADIUS, authoring::MAX_BRUSH_RADIUS);
            }

            if (!(spacing >= authoring::MIN_DAB_SPACING && spacing <= authoring::MAX_DAB_SPACING))
            {
                return std::format("spacing {} is outside [{}, {}] (a fraction of the radius)", spacing,
                                   authoring::MIN_DAB_SPACING, authoring::MAX_DAB_SPACING);
            }

            if (static_cast<uint8_t>(falloff) > static_cast<uint8_t>(terrain::BrushFalloff::Sharp))
                return std::format("unknown falloff {}", static_cast<unsigned>(falloff));

            if (static_cast<uint8_t>(shape) > static_cast<uint8_t>(terrain::BrushShape::Square))
                return std::format("unknown brush shape {}", static_cast<unsigned>(shape));

            return {};
        }

        bool isUnitInterval(float value)
        {
            return value >= 0.0f && value <= 1.0f; // false for NaN
        }

        // What a stroke learns about its dabs BEFORE it mutates anything, so the work budget and the
        // off-terrain test can both refuse up front.
        struct StrokePlan
        {
            std::vector<glm::vec2> dabs;
            std::vector<terrain::TileCoord> footprintTiles; // unique, in first-touch order
            uint32_t missingTiles = 0; // unique footprint tiles neither resident nor file-cached
            bool reachable = false;    // at least one footprint tile is resident or file-cached
            glm::vec2 footprintMin{0.0f};
            glm::vec2 footprintMax{0.0f};
        };

        // Resamples the polyline and walks every dab's BrushSampler footprint -- the very tiles the
        // cores will visit. The dab count is checked before anything is allocated, and the walk stops
        // at the first dab past the tile budget, so an absurd request costs at most one dab's worth of
        // coords. `passes` repeats the whole dab list (Smooth), so both budgets scale with it.
        TerrainStrokeStatus planStroke(const std::vector<glm::vec2>& points, float radius, float spacing,
                                       uint32_t passes, uint64_t tileDabBudget, float worldTileSize,
                                       const terrain::TerrainGrid& grid,
                                       const terrain::TerrainFileCache* fileCache,
                                       StrokePlan& plan, std::string& message)
        {
            const float maxSpacing = spacing * radius;
            const uint64_t dabCount =
                terrain::resampledDabCount(terrain::polylineLength(points), maxSpacing);

            if (dabCount > authoring::MAX_STROKE_DABS / passes)
            {
                message = std::format("the stroke needs {} dabs x {} pass(es), over the budget of {} "
                                      "dabs; shorten it, raise the spacing or use a larger radius",
                                      dabCount, passes, authoring::MAX_STROKE_DABS);
                return TerrainStrokeStatus::TooMuchWork;
            }

            plan.dabs = terrain::resamplePolyline(points, maxSpacing);

            std::unordered_set<terrain::TileCoord, terrain::TileCoordHash> seen;
            uint64_t tileDabs = 0;
            for (const glm::vec2& dab : plan.dabs)
            {
                const std::vector<terrain::TileCoord> coords =
                    terrain::BrushSampler::getAffectedTiles(dab, radius, worldTileSize);

                tileDabs += static_cast<uint64_t>(coords.size()) * passes;
                if (tileDabs > tileDabBudget)
                {
                    message = std::format("the stroke touches more than {} tile footprints in total; "
                                          "use a smaller radius or fewer points", tileDabBudget);
                    return TerrainStrokeStatus::TooMuchWork;
                }

                for (const terrain::TileCoord& coord : coords)
                {
                    if (!seen.insert(coord).second)
                        continue;

                    plan.footprintTiles.push_back(coord);
                    if (grid.hasTile(coord) || (fileCache && fileCache->hasCoord(coord)))
                        plan.reachable = true;
                    else
                        ++plan.missingTiles;
                }
            }

            plan.footprintMin = glm::vec2(std::numeric_limits<float>::max());
            plan.footprintMax = glm::vec2(std::numeric_limits<float>::lowest());
            for (const glm::vec2& dab : plan.dabs)
            {
                plan.footprintMin = glm::min(plan.footprintMin, dab - glm::vec2(radius));
                plan.footprintMax = glm::max(plan.footprintMax, dab + glm::vec2(radius));
            }

            return TerrainStrokeStatus::Ok;
        }

        std::string offTerrainMessage(const StrokePlan& plan, EntityHandle terrainEntity)
        {
            return std::format("the stroke footprint (x {} .. {}, z {} .. {}) does not reach any tile "
                               "of terrain {}", plan.footprintMin.x, plan.footprintMax.x,
                               plan.footprintMin.y, plan.footprintMax.y, terrainEntity.id);
        }

        // The cores take their footprint tile size from the first RESIDENT tile and fall back to
        // 32 m when there is none (TerrainBrushOps.cpp). After a reload with every tile streamed out,
        // that fallback would aim the dabs at the wrong coords on any other tile size, so stream one
        // footprint tile in first. A no-op whenever anything is resident, which is the normal case.
        void ensureOneResidentTile(TerrainService& service, EntityHandle terrainEntity,
                                   const terrain::TerrainGrid& grid,
                                   const terrain::TerrainFileCache* fileCache,
                                   const std::vector<terrain::TileCoord>& footprintTiles)
        {
            if (grid.getTileCount() > 0 || !fileCache)
                return;

            for (const terrain::TileCoord& coord : footprintTiles)
            {
                if (fileCache->hasCoord(coord) && service.streamInTile(terrainEntity, coord.x, coord.z))
                    return;
            }
        }

        // What the dabs of one stroke did, summed.
        struct StrokeTally
        {
            std::unordered_set<terrain::TileCoord, terrain::TileCoordHash> processed;
            uint32_t dabsApplied = 0;  // dabs that reached at least one tile
            uint64_t tileVisits = 0;   // tiles the dabs processed, counted per dab (GPU dispatches)
            uint64_t unloadedTiles = 0;
            uint64_t failedTiles = 0;
            uint64_t refusedTiles = 0;

            // A template only because the argument is TerrainService::DabOutcome, which is private to
            // the service; everything read here is a public field of it.
            template <typename Outcome>
            void add(const Outcome& outcome)
            {
                if (!outcome.modifiedTiles.empty())
                    ++dabsApplied;
                tileVisits += outcome.modifiedTiles.size();
                processed.insert(outcome.modifiedTiles.begin(), outcome.modifiedTiles.end());
                unloadedTiles += outcome.unloadedTiles;
                failedTiles += outcome.failedTiles;
                refusedTiles += outcome.refusedTiles;
            }

            // Footprint tiles no dab ever processed, each counted once however many dabs touched it.
            [[nodiscard]] uint32_t skipped(const std::vector<terrain::TileCoord>& footprintTiles) const
            {
                uint32_t count = 0;
                for (const terrain::TileCoord& coord : footprintTiles)
                {
                    if (processed.find(coord) == processed.end())
                        ++count;
                }
                return count;
            }
        };

        void appendStrokeWarnings(TerrainStrokeResult& result, const StrokePlan& plan,
                                  const StrokeTally& tally, bool painting, uint32_t layerIndex)
        {
            if (plan.missingTiles > 0)
            {
                result.warnings.push_back(std::format(
                    "{} tile(s) under the stroke lie outside the terrain and were skipped",
                    plan.missingTiles));
            }

            if (tally.unloadedTiles > 0)
            {
                result.warnings.push_back(painting
                    ? "some tiles under the stroke have no weight map loaded and were skipped"
                    : "some tiles under the stroke could not load their height data and were skipped");
            }

            if (tally.failedTiles > 0)
            {
                result.warnings.push_back(std::format(
                    "{} GPU brush dispatch(es) failed; the tiles concerned kept their previous heights",
                    tally.failedTiles));
            }

            if (tally.refusedTiles > 0)
            {
                result.warnings.push_back(std::format(
                    "layer {} was not painted on some tiles because each already blends {} layers; set "
                    "allowChannelEviction to evict the least-used layer of such a tile instead",
                    layerIndex, static_cast<unsigned>(terrain::WEIGHT_CHANNELS)));
            }
        }

        // Tile coord plus tile-local position for a world XZ point, in double precision like
        // TerrainService::getTerrainLayerWeightsAt (resolveTerrainSamplePosition) so a sample on a
        // seam lands on the same tile the queries pick.
        bool tileLocalPosition(const glm::vec2& worldXZ, float worldTileSize,
                               terrain::TileCoord& coord, float& localX, float& localZ)
        {
            if (!std::isfinite(worldXZ.x) || !std::isfinite(worldXZ.y)
                || !std::isfinite(worldTileSize) || worldTileSize <= 0.0f)
                return false;

            const double tileX = std::floor(static_cast<double>(worldXZ.x) / worldTileSize);
            const double tileZ = std::floor(static_cast<double>(worldXZ.y) / worldTileSize);
            constexpr double minCoord = static_cast<double>(std::numeric_limits<int32_t>::min());
            constexpr double maxCoord = static_cast<double>(std::numeric_limits<int32_t>::max());
            if (tileX < minCoord || tileX > maxCoord || tileZ < minCoord || tileZ > maxCoord)
                return false;

            coord = terrain::TileCoord(static_cast<int32_t>(tileX), static_cast<int32_t>(tileZ));
            localX = static_cast<float>(static_cast<double>(worldXZ.x) - tileX * worldTileSize);
            localZ = static_cast<float>(static_cast<double>(worldXZ.y) - tileZ * worldTileSize);
            return true;
        }

        // Before/after pairs for the reported points. A pair is valid only when both ends were.
        std::vector<TerrainStrokeSample> pairSamples(const std::vector<glm::vec2>& points,
                                                     const std::vector<TerrainHeightSample>& before,
                                                     const std::vector<TerrainHeightSample>& after)
        {
            std::vector<TerrainStrokeSample> samples;
            samples.reserve(points.size());
            for (size_t i = 0; i < points.size(); ++i)
            {
                TerrainStrokeSample sample;
                sample.xz = points[i];
                const bool haveBefore = i < before.size() && before[i].valid;
                const bool haveAfter = i < after.size() && after[i].valid;
                if (haveBefore)
                    sample.before = before[i].height;
                if (haveAfter)
                    sample.after = after[i].height;
                sample.valid = haveBefore && haveAfter;
                samples.push_back(sample);
            }
            return samples;
        }

        std::vector<glm::vec2> reportedPoints(const std::vector<glm::vec2>& points)
        {
            const size_t count = std::min<size_t>(points.size(), authoring::MAX_REPORTED_SAMPLES);
            return std::vector<glm::vec2>(points.begin(),
                                          points.begin() + static_cast<std::ptrdiff_t>(count));
        }

        void markTerrainSaveDirty(EntityHandle terrainEntity)
        {
            auto& registry = scene::EntityRegistry::getRegistry();
            const entt::entity ent = internal::fromHandle(terrainEntity);
            if (registry.valid(ent) && registry.all_of<components::TerrainComponent>(ent))
                registry.get<components::TerrainComponent>(ent).saveDirty = true;
        }

        // GLSL clamp (min(max(x, lo), hi)), the brush shader's final step. Unlike std::clamp it has no
        // precondition on the bounds, and it returns an in-range value bit for bit.
        float shaderClamp(float value, float lo, float hi)
        {
            return std::min(std::max(value, lo), hi);
        }
    }

    TerrainStrokeResult TerrainService::sculptTerrainStroke(
        const ::events::terrainAuthoring::SculptTerrainStrokeCommand& cmd)
    {
        if (cmd.points.size() > authoring::MAX_STROKE_POINTS)
        {
            return refuse(TerrainStrokeStatus::TooMuchWork,
                          std::format("{} points is over the limit of {} per stroke; split the stroke",
                                      cmd.points.size(), authoring::MAX_STROKE_POINTS));
        }

        std::string error = validateStroke(cmd.points, cmd.radius, cmd.spacing, cmd.falloff, cmd.shape);
        if (error.empty())
        {
            switch (cmd.op)
            {
            case authoring::SculptOp::Raise:
            case authoring::SculptOp::Lower:
                if (!(cmd.amount > 0.0f) || !std::isfinite(cmd.amount))
                    error = std::format("amount {} must be a finite number of metres above 0", cmd.amount);
                break;
            case authoring::SculptOp::Smooth:
                if (!isUnitInterval(cmd.strength))
                    error = std::format("strength {} is outside [0, 1]", cmd.strength);
                else if (cmd.passes < 1 || cmd.passes > authoring::MAX_SMOOTH_PASSES)
                    error = std::format("passes {} is outside [1, {}]", cmd.passes, authoring::MAX_SMOOTH_PASSES);
                break;
            case authoring::SculptOp::Flatten:
                if (!isUnitInterval(cmd.strength))
                    error = std::format("strength {} is outside [0, 1]", cmd.strength);
                else if (cmd.targetHeight && !std::isfinite(*cmd.targetHeight))
                    error = "targetHeight is not a finite number";
                break;
            default:
                error = std::format("unknown sculpt operation {}", static_cast<unsigned>(cmd.op));
                break;
            }
        }
        if (!error.empty())
            return refuse(TerrainStrokeStatus::InvalidArguments, std::move(error));

        if (saveInProgress.load(std::memory_order_acquire))
        {
            return refuse(TerrainStrokeStatus::SaveInProgress,
                          "a terrain save or load is in progress; retry once it has finished");
        }

        EntityHandle terrainEntity;
        std::string resolveError;
        terrain::TerrainGrid* grid = resolveAuthoringGrid(cmd.terrainEntity, terrainEntity, resolveError);
        if (!grid)
            return refuse(TerrainStrokeStatus::NoTerrain, std::move(resolveError));

        const terrain::TerrainTileConfig& config = grid->getTileConfig();
        const bool raiseOrLower =
            cmd.op == authoring::SculptOp::Raise || cmd.op == authoring::SculptOp::Lower;

        if (raiseOrLower && cmd.amount > config.maxHeight - config.minHeight)
        {
            return refuse(TerrainStrokeStatus::InvalidArguments,
                          std::format("amount {} m exceeds the terrain's height range of {} m ([{}, {}])",
                                      cmd.amount, config.maxHeight - config.minHeight,
                                      config.minHeight, config.maxHeight),
                          terrainEntity);
        }

        if (cmd.op == authoring::SculptOp::Flatten && cmd.targetHeight
            && (*cmd.targetHeight < config.minHeight || *cmd.targetHeight > config.maxHeight))
        {
            return refuse(TerrainStrokeStatus::InvalidArguments,
                          std::format("targetHeight {} is outside the terrain's height range [{}, {}]",
                                      *cmd.targetHeight, config.minHeight, config.maxHeight),
                          terrainEntity);
        }

        // Every sculpt op, Smooth and Flatten included, goes through brush_compute.glsl. The Runtime
        // never constructs the provider; this is an editor-only command in practice.
        if (!brushComputeProvider)
        {
            return refuse(TerrainStrokeStatus::GpuUnavailable,
                          "sculpting needs the GPU brush, which only the editor provides",
                          terrainEntity);
        }

        auto cacheIt = fileCaches.find(terrainEntity.id);
        const std::shared_ptr<terrain::TerrainFileCache> fileCache =
            (cacheIt != fileCaches.end()) ? cacheIt->second : nullptr;

        const uint32_t passes = (cmd.op == authoring::SculptOp::Smooth) ? cmd.passes : 1u;

        StrokePlan plan;
        std::string planError;
        const TerrainStrokeStatus planStatus =
            planStroke(cmd.points, cmd.radius, cmd.spacing, passes, authoring::MAX_GPU_TILE_DABS,
                       config.worldTileSize, *grid, fileCache.get(), plan, planError);
        if (planStatus != TerrainStrokeStatus::Ok)
            return refuse(planStatus, std::move(planError), terrainEntity);

        if (!plan.reachable)
            return refuse(TerrainStrokeStatus::OffTerrain, offTerrainMessage(plan, terrainEntity), terrainEntity);

        // The editor captures its flatten target from the first dab's surface hit; the authoring
        // equivalent is the height under the first point, sampled before anything moves.
        float flattenTarget = 0.0f;
        if (cmd.op == authoring::SculptOp::Flatten)
        {
            if (cmd.targetHeight)
            {
                flattenTarget = *cmd.targetHeight;
            }
            else
            {
                const std::vector<TerrainHeightSample> under = getTerrainHeights(
                    terrainEntity, std::vector<glm::vec2>{cmd.points.front()}, /*pageIn=*/true);
                if (under.empty() || !under.front().valid)
                {
                    return refuse(TerrainStrokeStatus::NoHeightAtPoint,
                                  std::format("there is no terrain height under the first point ({}, {}) "
                                              "to flatten to; pass targetHeight or start the stroke on "
                                              "the terrain", cmd.points.front().x, cmd.points.front().y),
                                  terrainEntity);
                }
                flattenTarget = under.front().height;
            }
        }

        // Raise/Lower add falloff * strength per dab, so the centreline of a swept stroke receives the
        // overlap sum S times the per-dab strength. Dividing by S makes the stroke's peak centreline
        // change equal `amount`, for one dab or a thousand (BrushStroke.hpp).
        terrain::BrushType brushType = terrain::BrushType::Raise;
        float perDabStrength = cmd.strength;
        switch (cmd.op)
        {
        case authoring::SculptOp::Raise:
            brushType = terrain::BrushType::Raise;
            perDabStrength = cmd.amount / terrain::strokePeakOverlap(plan.dabs, cmd.radius, cmd.falloff, cmd.shape);
            break;
        case authoring::SculptOp::Lower:
            brushType = terrain::BrushType::Lower;
            perDabStrength = cmd.amount / terrain::strokePeakOverlap(plan.dabs, cmd.radius, cmd.falloff, cmd.shape);
            break;
        case authoring::SculptOp::Smooth:
            brushType = terrain::BrushType::Smooth;
            break;
        case authoring::SculptOp::Flatten:
            brushType = terrain::BrushType::Flatten;
            break;
        }

        // Built field by field on purpose: BrushParams::validate() clamps strength to the editor
        // slider's [0, 100], which is not the unit an authoring stroke speaks.
        terrain::BrushParams brushParams;
        brushParams.radius = cmd.radius;
        brushParams.strength = perDabStrength;
        brushParams.falloff = cmd.falloff;
        brushParams.shape = cmd.shape;

        const std::string label = cmd.undoLabel.empty()
            ? std::string(strokeLabelFor(StrokeTool::Sculpt))
            : cmd.undoLabel;

        TerrainStrokeResult result;
        result.terrainEntity = terrainEntity;
        result.undoLabel = label;
        result.perDabStrength = perDabStrength;
        result.flattenTarget = (cmd.op == authoring::SculptOp::Flatten) ? flattenTarget : 0.0f;
        result.footprintMin = plan.footprintMin;
        result.footprintMax = plan.footprintMax;

        ensureOneResidentTile(*this, terrainEntity, *grid, fileCache.get(), plan.footprintTiles);

        const std::vector<glm::vec2> samplePoints = reportedPoints(cmd.points);
        const std::vector<TerrainHeightSample> before = getTerrainHeights(terrainEntity, samplePoints, true);
        std::vector<TerrainHeightSample> after;

        // A human stroke still open (mouse held mid-drag) is pushed as its own entry by this
        // isFirstApplication=true begin, ahead of ours; the human's next dab opens a fresh stroke.
        result.closedOpenStroke = strokeActive;
        beginStroke(terrainEntity.id, StrokeTool::Sculpt, /*isFirstApplication=*/true, label);

        StrokeTally tally;
        const auto started = std::chrono::steady_clock::now();
        try
        {
            for (uint32_t pass = 0; pass < passes; ++pass)
            {
                for (const glm::vec2& dab : plan.dabs)
                {
                    tally.add(applySculptDab(terrainEntity, grid, brushType, brushParams,
                                             glm::vec3(dab.x, 0.0f, dab.y), /*deltaTime=*/1.0f,
                                             /*invert=*/false, flattenTarget, glm::uvec2(0u),
                                             ColliderTiming::AtStrokeEnd));
                }
            }

            after = getTerrainHeights(terrainEntity, samplePoints, true);
        }
        catch (...)
        {
            // The dabs that landed before the throw are a real edit; push them so they can be undone.
            finalizeTerrainStroke();
            throw;
        }

        if (!tally.processed.empty())
            markTerrainSaveDirty(terrainEntity);

        const size_t pushedTiles = finalizeTerrainStroke();

        // Every GPU dispatch is a synchronous upload / dispatch / fence wait / readback, so this is
        // the number MAX_GPU_TILE_DABS has to be calibrated against.
        const double elapsedMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count();
        vfLogInfo("[TerrainAuthoring] Sculpt stroke on terrain {}: {} dab(s) x {} pass(es), {} tile "
                  "dispatch(es), {} tile(s) changed, {:.1f} ms",
                  terrainEntity.id, plan.dabs.size(), passes, tally.tileVisits, pushedTiles, elapsedMs);

        result.tilesChanged = static_cast<uint32_t>(pushedTiles);
        result.undoPushed = pushedTiles > 0;
        result.dabsApplied = tally.dabsApplied;
        result.tilesSkipped = tally.skipped(plan.footprintTiles);
        result.samples = pairSamples(samplePoints, before, after);
        appendStrokeWarnings(result, plan, tally, /*painting=*/false, 0);
        return result;
    }

    TerrainStrokeResult TerrainService::paintTerrainLayerStroke(
        const ::events::terrainAuthoring::PaintTerrainLayerStrokeCommand& cmd)
    {
        if (cmd.points.size() > authoring::MAX_STROKE_POINTS)
        {
            return refuse(TerrainStrokeStatus::TooMuchWork,
                          std::format("{} points is over the limit of {} per stroke; split the stroke",
                                      cmd.points.size(), authoring::MAX_STROKE_POINTS));
        }

        std::string error = validateStroke(cmd.points, cmd.radius, cmd.spacing, cmd.falloff, cmd.shape);
        if (error.empty())
        {
            if (cmd.op != authoring::PaintOp::Paint && cmd.op != authoring::PaintOp::Erase)
                error = std::format("unknown paint operation {}", static_cast<unsigned>(cmd.op));
            else if (!isUnitInterval(cmd.strength))
                error = std::format("strength {} is outside [0, 1]", cmd.strength);
            else if (cmd.layerIndex >= static_cast<uint32_t>(terrain::MAX_TERRAIN_LAYERS))
                error = std::format("layer {} is outside [0, {}]", cmd.layerIndex, terrain::MAX_TERRAIN_LAYERS - 1);
        }
        if (!error.empty())
            return refuse(TerrainStrokeStatus::InvalidArguments, std::move(error));

        if (saveInProgress.load(std::memory_order_acquire))
        {
            return refuse(TerrainStrokeStatus::SaveInProgress,
                          "a terrain save or load is in progress; retry once it has finished");
        }

        EntityHandle terrainEntity;
        std::string resolveError;
        terrain::TerrainGrid* grid = resolveAuthoringGrid(cmd.terrainEntity, terrainEntity, resolveError);
        if (!grid)
            return refuse(TerrainStrokeStatus::NoTerrain, std::move(resolveError));

        const terrain::TerrainTileConfig& config = grid->getTileConfig();

        auto cacheIt = fileCaches.find(terrainEntity.id);
        const std::shared_ptr<terrain::TerrainFileCache> fileCache =
            (cacheIt != fileCaches.end()) ? cacheIt->second : nullptr;

        StrokePlan plan;
        std::string planError;
        const TerrainStrokeStatus planStatus =
            planStroke(cmd.points, cmd.radius, cmd.spacing, /*passes=*/1u, authoring::MAX_CPU_TILE_DABS,
                       config.worldTileSize, *grid, fileCache.get(), plan, planError);
        if (planStatus != TerrainStrokeStatus::Ok)
            return refuse(planStatus, std::move(planError), terrainEntity);

        if (!plan.reachable)
            return refuse(TerrainStrokeStatus::OffTerrain, offTerrainMessage(plan, terrainEntity), terrainEntity);

        // Paint is additive per dab exactly like Raise (w += falloff * strength * opacity), so the
        // same overlap normalisation makes `strength` the weight added along the centreline.
        const float perDabStrength =
            cmd.strength / terrain::strokePeakOverlap(plan.dabs, cmd.radius, cmd.falloff, cmd.shape);

        terrain::PaintBrushParams brushParams;
        brushParams.radius = cmd.radius;
        brushParams.strength = perDabStrength;
        brushParams.opacity = 1.0f;
        brushParams.activeLayer = cmd.layerIndex;
        brushParams.falloff = cmd.falloff;
        brushParams.shape = cmd.shape;
        brushParams.target = terrain::PaintTarget::Layers;

        const terrain::PaintBrushType brushType = (cmd.op == authoring::PaintOp::Erase)
            ? terrain::PaintBrushType::EraseLayer
            : terrain::PaintBrushType::PaintLayer;

        // An erase never has a reason to evict: in Allow mode WeightBrushApplicator would assign --
        // and possibly evict -- a channel for a layer the tile does not even carry, then erase nothing
        // from it. Refuse turns that case into the no-op it should be, whatever the caller allowed.
        const ChannelEviction eviction = (cmd.op == authoring::PaintOp::Erase || !cmd.allowChannelEviction)
            ? ChannelEviction::Refuse
            : ChannelEviction::Allow;

        const std::string label = cmd.undoLabel.empty()
            ? std::string(strokeLabelFor(StrokeTool::Paint))
            : cmd.undoLabel;

        TerrainStrokeResult result;
        result.terrainEntity = terrainEntity;
        result.undoLabel = label;
        result.perDabStrength = perDabStrength;
        result.footprintMin = plan.footprintMin;
        result.footprintMax = plan.footprintMax;

        ensureOneResidentTile(*this, terrainEntity, *grid, fileCache.get(), plan.footprintTiles);

        // The painted layer's weight under each reported point, on the RESOLVED grid. Tiles are paged
        // in the way the paint core pages them, so "before" exists for a tile the stroke is about to
        // stream in; a tile that cannot be read leaves its sample invalid.
        const auto paletteLayer = static_cast<uint8_t>(cmd.layerIndex);
        auto sampleLayerWeights = [&](const std::vector<glm::vec2>& positions)
        {
            std::vector<TerrainHeightSample> samples(positions.size());
            for (size_t i = 0; i < positions.size(); ++i)
            {
                terrain::TileCoord coord;
                float localX = 0.0f;
                float localZ = 0.0f;
                if (!tileLocalPosition(positions[i], config.worldTileSize, coord, localX, localZ))
                    continue;

                terrain::TerrainTile* tile = grid->getTile(coord);
                if (!tile && fileCache && fileCache->hasCoord(coord))
                {
                    streamInTile(terrainEntity, coord.x, coord.z);
                    tile = grid->getTile(coord);
                }
                if (tile && fileCache && !tile->hasHeightData())
                    fileCache->ensureHeightsLoaded(*tile);
                if (!tile || !tile->hasWeightMap())
                    continue;

                samples[i].height = terrain::sampleTileLayerWeightBilinear(
                    tile->weightMap, paletteLayer, localX, localZ, config.getVertexSpacing());
                samples[i].valid = true;
                samples[i].onTerrain = true;
            }
            return samples;
        };

        const std::vector<glm::vec2> samplePoints = reportedPoints(cmd.points);
        const std::vector<TerrainHeightSample> before = sampleLayerWeights(samplePoints);
        std::vector<TerrainHeightSample> after;

        result.closedOpenStroke = strokeActive;
        beginStroke(terrainEntity.id, StrokeTool::Paint, /*isFirstApplication=*/true, label);

        StrokeTally tally;
        const auto started = std::chrono::steady_clock::now();
        try
        {
            for (const glm::vec2& dab : plan.dabs)
            {
                tally.add(applyLayerPaintDab(terrainEntity, grid, brushType, brushParams,
                                             glm::vec3(dab.x, 0.0f, dab.y), /*deltaTime=*/1.0f,
                                             /*invert=*/false, eviction));
            }

            after = sampleLayerWeights(samplePoints);
        }
        catch (...)
        {
            finalizeTerrainStroke();
            throw;
        }

        if (!tally.processed.empty())
            markTerrainSaveDirty(terrainEntity);

        const size_t pushedTiles = finalizeTerrainStroke();

        // The CPU counterpart of the sculpt log: what MAX_CPU_TILE_DABS is calibrated against.
        const double elapsedMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count();
        vfLogInfo("[TerrainAuthoring] Paint stroke on terrain {}: layer {}, {} dab(s), {} tile "
                  "visit(s), {} tile(s) changed, {:.1f} ms",
                  terrainEntity.id, cmd.layerIndex, plan.dabs.size(), tally.tileVisits, pushedTiles,
                  elapsedMs);

        result.tilesChanged = static_cast<uint32_t>(pushedTiles);
        result.undoPushed = pushedTiles > 0;
        result.dabsApplied = tally.dabsApplied;
        result.tilesSkipped = tally.skipped(plan.footprintTiles);
        result.samples = pairSamples(samplePoints, before, after);
        appendStrokeWarnings(result, plan, tally, /*painting=*/true, cmd.layerIndex);
        return result;
    }

    TerrainStrokeResult TerrainService::applyHeightmapToTerrain(
        const ::events::terrainAuthoring::ApplyHeightmapCommand& cmd)
    {
        if (cmd.heightmapPath.empty())
            return refuse(TerrainStrokeStatus::InvalidArguments, "heightmapPath is empty");

        if (!std::isfinite(cmd.baseHeight))
            return refuse(TerrainStrokeStatus::InvalidArguments, "baseHeight is not a finite number");

        if (!(cmd.amplitude > 0.0f) || !std::isfinite(cmd.amplitude)
            || !std::isfinite(cmd.baseHeight + cmd.amplitude))
        {
            return refuse(TerrainStrokeStatus::InvalidArguments,
                          std::format("amplitude {} must be a finite number of metres above 0", cmd.amplitude));
        }

        if (saveInProgress.load(std::memory_order_acquire))
        {
            return refuse(TerrainStrokeStatus::SaveInProgress,
                          "a terrain save or load is in progress; retry once it has finished");
        }

        EntityHandle terrainEntity;
        std::string resolveError;
        terrain::TerrainGrid* grid = resolveAuthoringGrid(cmd.terrainEntity, terrainEntity, resolveError);
        if (!grid)
            return refuse(TerrainStrokeStatus::NoTerrain, std::move(resolveError));

        const terrain::TerrainTileConfig& config = grid->getTileConfig();

        auto cacheIt = fileCaches.find(terrainEntity.id);
        const std::shared_ptr<terrain::TerrainFileCache> fileCache =
            (cacheIt != fileCaches.end()) ? cacheIt->second : nullptr;

        // Every tile the terrain owns: the resident ones plus those only the file cache holds. Sorted
        // so the write order -- and with it the seam welds -- is deterministic.
        std::unordered_set<terrain::TileCoord, terrain::TileCoordHash> coordSet;
        grid->forEachTile([&coordSet](const terrain::TerrainTile& tile) { coordSet.insert(tile.coord); });
        if (fileCache)
        {
            for (const terrain::TileCoord& coord : fileCache->getSavedCoords())
                coordSet.insert(coord);
        }
        std::vector<terrain::TileCoord> coords(coordSet.begin(), coordSet.end());
        std::sort(coords.begin(), coords.end(),
                  [](const terrain::TileCoord& a, const terrain::TileCoord& b)
                  {
                      return (a.z != b.z) ? (a.z < b.z) : (a.x < b.x);
                  });

        if (coords.empty())
        {
            return refuse(TerrainStrokeStatus::OffTerrain,
                          std::format("terrain {} has no tiles to write", terrainEntity.id), terrainEntity);
        }

        const uint64_t vertexCount = config.getVertexCount();
        const uint64_t totalVertices = static_cast<uint64_t>(coords.size()) * vertexCount * vertexCount;
        if (totalVertices > authoring::MAX_TERRAIN_VERTICES)
        {
            return refuse(TerrainStrokeStatus::TooMuchWork,
                          std::format("the terrain has {} vertices, over the apply budget of {}",
                                      totalVertices, authoring::MAX_TERRAIN_VERTICES),
                          terrainEntity);
        }

        const std::shared_ptr<terrain::HeightmapData> heightmap = terrain::HeightmapLoader::load(cmd.heightmapPath);
        if (!heightmap || !heightmap->isValid())
        {
            return refuse(TerrainStrokeStatus::InvalidArguments,
                          std::format("could not load '{}' as a .vfImage heightmap", cmd.heightmapPath),
                          terrainEntity);
        }

        // The grid's full world rect, mapped EXACTLY as createTerrain maps a creation heightmap
        // (TerrainCreationOps.cpp): the same TerrainBounds arithmetic over the tile extent, the same
        // createHeightSamplerFromMap, and below the same per-vertex world position the tile generator
        // samples at. That is what makes apply(h) on a flat terrain bit-identical to creating the
        // terrain with h -- only the height range differs, [baseHeight, baseHeight + amplitude].
        int32_t minX = coords.front().x;
        int32_t maxX = coords.front().x;
        int32_t minZ = coords.front().z;
        int32_t maxZ = coords.front().z;
        for (const terrain::TileCoord& coord : coords)
        {
            minX = std::min(minX, coord.x);
            maxX = std::max(maxX, coord.x);
            minZ = std::min(minZ, coord.z);
            maxZ = std::max(maxZ, coord.z);
        }

        const float terrainMinX = static_cast<float>(minX) * config.worldTileSize;
        const float terrainMinZ = static_cast<float>(minZ) * config.worldTileSize;
        const float terrainWidth = static_cast<float>(maxX - minX + 1) * config.worldTileSize;
        const float terrainDepth = static_cast<float>(maxZ - minZ + 1) * config.worldTileSize;

        const terrain::TerrainBounds bounds{terrainMinX, terrainMinZ, terrainWidth, terrainDepth,
                                            cmd.baseHeight, cmd.baseHeight + cmd.amplitude};
        const terrain::HeightSampler sampler = terrain::createHeightSamplerFromMap(heightmap, bounds);

        const std::string label = cmd.undoLabel.empty() ? std::string(APPLY_HEIGHTMAP_LABEL) : cmd.undoLabel;

        TerrainStrokeResult result;
        result.terrainEntity = terrainEntity;
        result.undoLabel = label;
        result.footprintMin = glm::vec2(terrainMinX, terrainMinZ);
        result.footprintMax = glm::vec2(terrainMinX + terrainWidth, terrainMinZ + terrainDepth);

        constexpr uint8_t heightKind =
            ::events::terrain::strokeKindBit(::events::terrain::StrokeDataKind::Heights);

        std::vector<terrain::TileCoord> modifiedTiles;
        uint32_t missingTiles = 0;
        uint32_t unloadedTiles = 0;

        result.closedOpenStroke = strokeActive;
        beginStroke(terrainEntity.id, StrokeTool::Sculpt, /*isFirstApplication=*/true, label);

        const auto started = std::chrono::steady_clock::now();
        try
        {
            for (const terrain::TileCoord& coord : coords)
            {
                terrain::TerrainTile* tile = grid->getTile(coord);
                if (!tile && fileCache && fileCache->hasCoord(coord))
                {
                    streamInTile(terrainEntity, coord.x, coord.z);
                    tile = grid->getTile(coord);
                }
                if (!tile)
                {
                    ++missingTiles;
                    continue;
                }

                if (fileCache && !tile->hasHeightData() && !fileCache->ensureHeightsLoaded(*tile))
                {
                    ++unloadedTiles;
                    continue;
                }
                if (!tile->hasHeightData())
                {
                    ++unloadedTiles;
                    continue;
                }
                if (fileCache)
                    fileCache->markDirty(coord);

                // Same order as the sculpt core: snapshot, then route to the authoritative plane.
                captureStrokeTileBefore(grid, *tile, heightKind);

                // VK-1645: on a tile under a reserved height layer the BASE is what gets replaced;
                // the recompose below rebuilds the composite on top of it.
                std::vector<float>& authoritative = authoritativeHeights(grid->getHeightLayers(), *tile);
                const bool editingBase = (&authoritative != &tile->heightData);

                const uint32_t tileVertexCount = tile->config.getVertexCount();
                if (authoritative.size() != static_cast<size_t>(tileVertexCount) * tileVertexCount)
                {
                    ++unloadedTiles;
                    continue;
                }

                // TerrainTileGenerator::generateTile's sampling, verbatim.
                const glm::vec3 origin = tile->computeWorldOrigin();
                const float spacing = tile->config.getVertexSpacing();
                for (uint32_t z = 0; z < tileVertexCount; ++z)
                {
                    for (uint32_t x = 0; x < tileVertexCount; ++x)
                    {
                        const float worldX = origin.x + static_cast<float>(x) * spacing;
                        const float worldZ = origin.z + static_cast<float>(z) * spacing;
                        authoritative[static_cast<size_t>(z) * tileVertexCount + x] =
                            shaderClamp(sampler(worldX, worldZ), tile->config.minHeight, tile->config.maxHeight);
                    }
                }

                if (editingBase)
                    grid->getHeightLayers().markBaseDirty(coord);

                tile->isDirty = true;
                tile->setAllLODsDirty();
                modifiedTiles.push_back(coord);
            }

            // Derived output first, then the seams, exactly like a sculpt dab. Both sides of a seam
            // sampled the same world position, so on an uncovered terrain the weld is a bit-identity.
            recomposeAfterAuthoritativeEdit(grid, modifiedTiles);
            syncBrushBoundaryHeights(grid, modifiedTiles);
            deferStrokeColliders(terrainEntity, modifiedTiles);

            // One notification per tile: the navmesh dirties the tiles around the notified position
            // only, so a single terrain-wide one would leave most of it stale.
            auto& dispatcher = ::events::EventDispatcher::instance();
            const float halfTile = 0.5f * config.worldTileSize;
            for (const terrain::TileCoord& coord : modifiedTiles)
            {
                ::events::brush::BrushAppliedNotification notification;
                notification.position = glm::vec3(
                    static_cast<float>(coord.x) * config.worldTileSize + halfTile,
                    0.0f,
                    static_cast<float>(coord.z) * config.worldTileSize + halfTile);
                notification.type = terrain::BrushType::Stamp; // a heightmap stamped over the terrain
                dispatcher.publish(notification);
            }
        }
        catch (...)
        {
            finalizeTerrainStroke();
            throw;
        }

        if (!modifiedTiles.empty())
            markTerrainSaveDirty(terrainEntity);

        const size_t pushedTiles = finalizeTerrainStroke();

        const double elapsedMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count();
        vfLogInfo("[TerrainAuthoring] Heightmap applied to terrain {}: {} tile(s) written, {} changed, "
                  "{:.1f} ms", terrainEntity.id, modifiedTiles.size(), pushedTiles, elapsedMs);

        result.tilesChanged = static_cast<uint32_t>(pushedTiles);
        result.undoPushed = pushedTiles > 0;
        result.tilesSkipped = missingTiles + unloadedTiles;

        if (missingTiles > 0)
        {
            result.warnings.push_back(std::format(
                "{} tile(s) could not be streamed in and were left unchanged", missingTiles));
        }
        if (unloadedTiles > 0)
        {
            result.warnings.push_back(std::format(
                "{} tile(s) have no usable height data and were left unchanged", unloadedTiles));
        }

        return result;
    }
}
