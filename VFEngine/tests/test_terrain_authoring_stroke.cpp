// VK-1653 — the terrain authoring strokes (MCP P4) end to end, through the REAL handlers.
//
// A live services::TerrainService over a real multi-tile TerrainGrid, driven through the dispatcher
// the way the MCP tools drive it (fixture: test_terrain_layer_stack_ops.cpp). The one fake is the GPU
// brush: CpuBrushCompute transcribes brush_compute.glsl's Raise / Lower / Smooth / Flatten cases, so
// the sculpt cores run exactly as in the editor minus the Vulkan device.
//
// The terrain is 2x2 Low tiles centred on the origin -- tiles x, z in {-1, 0}, world [-32, 32)^2,
// 1 m vertex spacing -- so every integral world position is a vertex and expected heights can be
// worked by hand. Height comparisons on whole planes are bit-exact: undo restores bytes, and the
// heightmap apply is specified to reproduce terrain creation bit for bit.

#include <doctest.h>

#include "events/EventDispatcher.hpp"
#include "events/editor/SculptModeEvents.hpp"
#include "events/editor/UndoRedoEvents.hpp"
#include "events/terrain/BrushEvents.hpp"
#include "events/terrain/PaintBrushEvents.hpp"
#include "events/terrain/PaintModeEvents.hpp"
#include "events/terrain/TerrainAuthoringEvents.hpp"
#include "events/terrain/TerrainEvents.hpp"
#include "events/terrain/TerrainStrokeEvents.hpp"
#include "impl/scene/TerrainService.hpp"
#include "providers/terrain/ITerrainBrushComputeProvider.hpp"
#include <components/Components.hpp>
#include <data/EntityConversion.hpp>
#include <data/UndoTypes.hpp>
#include <procedural/generator/HeightmapGenerator.hpp>
#include <scene/EntityRegistry.hpp>
#include <scene/SceneGraphSystem.hpp>
#include <terrain/BrushFalloff.hpp>
#include <terrain/HeightmapLoader.hpp>
#include <terrain/TerrainTile.hpp>
#include <terrain/TileHeightSampler.hpp>

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace
{
    namespace authoring = events::terrainAuthoring;
    using Status = services::TerrainStrokeStatus;

    // ---- Fixture pieces, as in test_terrain_layer_stack_ops.cpp ----

    // Captures whatever gets pushed, without claiming the undo service's slots.
    struct UndoLog
    {
        std::vector<std::shared_ptr<services::IUndoableCommand>> pushed;

        [[nodiscard]] bool empty() const { return pushed.empty(); }
        [[nodiscard]] size_t size() const { return pushed.size(); }
        [[nodiscard]] services::IUndoableCommand& last() const { return *pushed.back(); }
    };

    class UndoCaptureGuard
    {
    public:
        explicit UndoCaptureGuard(UndoLog& log)
        {
            auto& d = events::EventDispatcher::instance();

            d.unregisterCommandHandler<events::undoredo::PushUndoableCommand>();
            d.registerCommandHandler<events::undoredo::PushUndoableCommand>(
                [&log](const events::undoredo::PushUndoableCommand& cmd)
                {
                    if (cmd.command)
                        log.pushed.push_back(cmd.command);
                });

            // The authoring strokes must never batch; these only exist so a stray call is harmless.
            d.unregisterCommandHandler<events::undoredo::BeginBatchCommand>();
            d.registerCommandHandler<events::undoredo::BeginBatchCommand>(
                [](const events::undoredo::BeginBatchCommand&) {});

            d.unregisterCommandHandler<events::undoredo::EndBatchCommand>();
            d.registerCommandHandler<events::undoredo::EndBatchCommand>(
                [](const events::undoredo::EndBatchCommand&) {});
        }

        ~UndoCaptureGuard()
        {
            auto& d = events::EventDispatcher::instance();
            d.unregisterCommandHandler<events::undoredo::PushUndoableCommand>();
            d.unregisterCommandHandler<events::undoredo::BeginBatchCommand>();
            d.unregisterCommandHandler<events::undoredo::EndBatchCommand>();
        }

        UndoCaptureGuard(const UndoCaptureGuard&) = delete;
        UndoCaptureGuard& operator=(const UndoCaptureGuard&) = delete;
    };

    // Deletes a terrain BEFORE the service that owns its grid. Load-bearing: createTerrain puts the
    // terrain and its tiles into the process-global EntityRegistry, and ~TerrainService does not
    // remove them (see test_terrain_layer_stack_ops.cpp for the crash this prevents).
    class ScopedTerrain
    {
    public:
        ScopedTerrain(services::TerrainService& owner, services::EntityHandle terrain)
            : service(owner), entity(terrain)
        {
        }

        ~ScopedTerrain() { service.deleteTerrain(entity); }

        ScopedTerrain(const ScopedTerrain&) = delete;
        ScopedTerrain& operator=(const ScopedTerrain&) = delete;

    private:
        services::TerrainService& service;
        services::EntityHandle entity;
    };

    // brush_compute.glsl cases 0-3 (Raise, Lower, Smooth, Flatten) on the CPU: the exclusive rim,
    // circle / square distance, falloff, and the final clamp, with Smooth reading the pre-dispatch
    // plane like the shader's heightsIn. Every other brush type falls through to the clamp, as in the
    // shader. Records every dispatch so the editor-wrapper checks can inspect the parameters.
    class CpuBrushCompute : public services::ITerrainBrushComputeProvider
    {
    public:
        std::vector<terrain::BrushGPUParams> dispatches;

        bool applyBrushGPU(std::vector<float>& heightData, const terrain::BrushGPUParams& params) override
        {
            dispatches.push_back(params);

            const uint32_t n = params.verticesPerSide;
            if (n == 0 || heightData.size() != static_cast<size_t>(n) * n)
                return false;

            const std::vector<float> heightsIn = heightData;

            for (uint32_t z = 0; z < n; ++z)
            {
                for (uint32_t x = 0; x < n; ++x)
                {
                    const size_t idx = static_cast<size_t>(z) * n + x;
                    const float currentHeight = heightsIn[idx];

                    const glm::vec2 worldPos = params.tileWorldOrigin
                        + glm::vec2(static_cast<float>(x), static_cast<float>(z)) * params.vertexSpacing;
                    const glm::vec2 delta = worldPos - params.brushCenter;

                    float dist = 0.0f;
                    if (params.brushType == terrain::BrushType::Stamp)
                        dist = std::max(std::abs(delta.x), std::abs(delta.y)) / params.brushRadius;
                    else if (params.shape == terrain::BrushShape::Circle)
                        dist = glm::length(delta) / params.brushRadius;
                    else
                        dist = std::max(std::abs(delta.x), std::abs(delta.y)) / params.brushRadius;

                    if (dist >= 1.0f)
                    {
                        heightData[idx] = currentHeight; // outside: copied through, NOT clamped
                        continue;
                    }

                    const float influence = terrain::applyFalloff(dist, params.falloff);
                    float newHeight = currentHeight;

                    switch (params.brushType)
                    {
                    case terrain::BrushType::Raise:
                    {
                        const float direction = params.invert ? -1.0f : 1.0f;
                        newHeight += direction * influence * params.brushStrength * params.deltaTime;
                        break;
                    }
                    case terrain::BrushType::Lower:
                    {
                        const float direction = params.invert ? 1.0f : -1.0f;
                        newHeight += direction * influence * params.brushStrength * params.deltaTime;
                        break;
                    }
                    case terrain::BrushType::Smooth:
                    {
                        float sum = 0.0f;
                        float count = 0.0f;
                        for (int dz = -1; dz <= 1; ++dz)
                        {
                            for (int dx = -1; dx <= 1; ++dx)
                            {
                                if (dx == 0 && dz == 0)
                                    continue;
                                const int nx = static_cast<int>(x) + dx;
                                const int nz = static_cast<int>(z) + dz;
                                if (nx >= 0 && nx < static_cast<int>(n) && nz >= 0 && nz < static_cast<int>(n))
                                {
                                    sum += heightsIn[static_cast<size_t>(nz) * n + static_cast<size_t>(nx)];
                                    count += 1.0f;
                                }
                            }
                        }
                        if (count > 0.0f)
                        {
                            const float avgHeight = sum / count;
                            const float smoothFactor = std::clamp(
                                influence * params.brushStrength * params.deltaTime, 0.0f, 1.0f);
                            newHeight = currentHeight * (1.0f - smoothFactor) + avgHeight * smoothFactor; // mix
                        }
                        break;
                    }
                    case terrain::BrushType::Flatten:
                    {
                        const float flattenFactor = std::clamp(
                            influence * params.brushStrength * params.deltaTime, 0.0f, 1.0f);
                        newHeight = currentHeight * (1.0f - flattenFactor) + params.targetHeight * flattenFactor;
                        break;
                    }
                    default:
                        break;
                    }

                    heightData[idx] = std::clamp(newHeight, params.minHeight, params.maxHeight);
                }
            }
            return true;
        }

        bool applyHydraulicErosionGPU(std::vector<float>&, const std::vector<uint32_t>&,
                                      const terrain::HydraulicGPUParams&) override
        {
            return false;
        }

        void setStampData(const std::vector<float>&, uint32_t, uint32_t) override {}
        void clearStampData() override {}
    };

    // 2x2 Low tiles centred on the origin. More than one tile on purpose: createGrid's LOD pass only
    // reaches the JobSystem path with several tiles (test_terrain_layer_stack_ops.cpp), and the seam
    // cases need two.
    services::TerrainCreationData quadTerrain()
    {
        services::TerrainCreationData creation;
        creation.tilesX = 2;
        creation.tilesZ = 2;
        creation.resolution = 0; // Low: 33 verts, 32 quads
        creation.worldTileSize = 32.0f;
        creation.minHeight = -10.0f;
        creation.maxHeight = 100.0f;
        return creation;
    }

    struct ClearedDispatcher
    {
        // Cleared on the way IN, the suite's convention (see test_terrain_layer_stack_ops.cpp).
        ClearedDispatcher() { events::EventDispatcher::instance().clear(); }
    };

    // One live service with its handlers registered and, optionally, the CPU brush and a terrain.
    // Declaration order is construction order: the handler table is cleared before anything
    // registers, and in reverse the terrain is deleted while the service that owns its grid lives.
    struct LiveTerrain
    {
        ClearedDispatcher cleared;
        CpuBrushCompute gpu;
        services::TerrainService service;
        UndoLog undoLog;
        UndoCaptureGuard undoGuard;
        services::EntityHandle entity;
        std::optional<ScopedTerrain> terrainGuard;

        explicit LiveTerrain(bool withTerrain = true, bool withGpu = true)
            : service(std::make_shared<scene::SceneGraphSystem>())
            , undoGuard(undoLog)
        {
            service.registerEventHandlers();
            if (withGpu)
                service.setBrushComputeProvider(&gpu);
            if (withTerrain)
            {
                entity = service.createTerrain(quadTerrain());
                if (entity.isValid())
                    terrainGuard.emplace(service, entity);
            }
        }

        LiveTerrain(const LiveTerrain&) = delete;
        LiveTerrain& operator=(const LiveTerrain&) = delete;
    };

    // Stands in for the editor's sculpt-mode and brush-settings services, which a CPU test does not
    // construct. Read on every ApplyBrushCommand, so a test may change the fields between dabs.
    struct SculptModeFake
    {
        services::EntityHandle target;
        terrain::BrushType type = terrain::BrushType::Raise;
        terrain::BrushParams params;
        std::shared_ptr<terrain::HeightmapData> stamp;

        explicit SculptModeFake(services::EntityHandle terrainEntity)
            : target(terrainEntity)
        {
            auto& d = events::EventDispatcher::instance();
            d.registerQueryHandler<events::sculpt::GetSculptTargetEntityQuery>(
                [this](const events::sculpt::GetSculptTargetEntityQuery&) -> std::optional<services::EntityHandle>
                {
                    return target;
                });
            d.registerQueryHandler<events::brush::GetBrushTypeQuery>(
                [this](const events::brush::GetBrushTypeQuery&) { return type; });
            d.registerQueryHandler<events::brush::GetBrushParamsQuery>(
                [this](const events::brush::GetBrushParamsQuery&) { return params; });
            d.registerQueryHandler<events::brush::GetStampDataQuery>(
                [this](const events::brush::GetStampDataQuery&) { return stamp; });
        }

        ~SculptModeFake()
        {
            auto& d = events::EventDispatcher::instance();
            d.unregisterQueryHandler<events::sculpt::GetSculptTargetEntityQuery>();
            d.unregisterQueryHandler<events::brush::GetBrushTypeQuery>();
            d.unregisterQueryHandler<events::brush::GetBrushParamsQuery>();
            d.unregisterQueryHandler<events::brush::GetStampDataQuery>();
        }

        SculptModeFake(const SculptModeFake&) = delete;
        SculptModeFake& operator=(const SculptModeFake&) = delete;
    };

    struct PaintModeFake
    {
        services::EntityHandle target;
        terrain::PaintBrushType type = terrain::PaintBrushType::PaintLayer;
        terrain::PaintBrushParams params;

        explicit PaintModeFake(services::EntityHandle terrainEntity)
            : target(terrainEntity)
        {
            auto& d = events::EventDispatcher::instance();
            d.registerQueryHandler<events::paint::GetPaintTargetEntityQuery>(
                [this](const events::paint::GetPaintTargetEntityQuery&) -> std::optional<services::EntityHandle>
                {
                    return target;
                });
            d.registerQueryHandler<events::paintBrush::GetPaintBrushTypeQuery>(
                [this](const events::paintBrush::GetPaintBrushTypeQuery&) { return type; });
            d.registerQueryHandler<events::paintBrush::GetPaintBrushParamsQuery>(
                [this](const events::paintBrush::GetPaintBrushParamsQuery&) { return params; });
        }

        ~PaintModeFake()
        {
            auto& d = events::EventDispatcher::instance();
            d.unregisterQueryHandler<events::paint::GetPaintTargetEntityQuery>();
            d.unregisterQueryHandler<events::paintBrush::GetPaintBrushTypeQuery>();
            d.unregisterQueryHandler<events::paintBrush::GetPaintBrushParamsQuery>();
        }

        PaintModeFake(const PaintModeFake&) = delete;
        PaintModeFake& operator=(const PaintModeFake&) = delete;
    };

    class TempDir
    {
    public:
        explicit TempDir(const std::string& name)
            : path(std::filesystem::temp_directory_path() / name)
        {
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
            std::filesystem::create_directories(path, ec);
        }

        ~TempDir()
        {
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }

        TempDir(const TempDir&) = delete;
        TempDir& operator=(const TempDir&) = delete;

        std::filesystem::path path;
    };

    // ---- Helpers ----

    events::EventDispatcher& dispatcher()
    {
        return events::EventDispatcher::instance();
    }

    // Every tile's height plane, concatenated in a fixed coord order -- seams included.
    std::vector<float> wholeTerrain(services::TerrainService& service)
    {
        auto tiles = service.getAllLoadedTiles();
        std::sort(tiles.begin(), tiles.end(),
                  [](const terrain::TerrainTile* a, const terrain::TerrainTile* b)
                  {
                      if (a->coord.z != b->coord.z)
                          return a->coord.z < b->coord.z;
                      return a->coord.x < b->coord.x;
                  });

        std::vector<float> all;
        for (const terrain::TerrainTile* tile : tiles)
            all.insert(all.end(), tile->heightData.begin(), tile->heightData.end());
        return all;
    }

    [[nodiscard]] bool differsSomewhere(const std::vector<float>& a, const std::vector<float>& b)
    {
        return a.size() != b.size() || !std::equal(a.begin(), a.end(), b.begin());
    }

    terrain::TerrainTile* findTile(services::TerrainService& service, int32_t x, int32_t z)
    {
        for (terrain::TerrainTile* tile : service.getAllLoadedTiles())
        {
            if (tile && tile->coord.x == x && tile->coord.z == z)
                return tile;
        }
        return nullptr;
    }

    // The vertex at an integral world position. With 32 m tiles and 1 m spacing, world (x, z) is
    // vertex (x - 32 tx, z - 32 tz) of tile (tx, tz) = floor(/ 32); a seam vertex is read from the
    // tile whose MIN edge it is.
    float vertexHeight(services::TerrainService& service, int32_t worldX, int32_t worldZ)
    {
        const int32_t tileX = static_cast<int32_t>(std::floor(static_cast<double>(worldX) / 32.0));
        const int32_t tileZ = static_cast<int32_t>(std::floor(static_cast<double>(worldZ) / 32.0));
        const terrain::TerrainTile* tile = findTile(service, tileX, tileZ);
        REQUIRE(tile != nullptr);
        REQUIRE(tile->heightData.size() == 33u * 33u);
        const auto localX = static_cast<size_t>(worldX - tileX * 32);
        const auto localZ = static_cast<size_t>(worldZ - tileZ * 32);
        return tile->heightData[localZ * 33 + localX];
    }

    // Palette layer `layer`'s weight at an integral world position (exactly the texel there).
    float layerWeight(services::TerrainService& service, int32_t worldX, int32_t worldZ, uint8_t layer)
    {
        const int32_t tileX = static_cast<int32_t>(std::floor(static_cast<double>(worldX) / 32.0));
        const int32_t tileZ = static_cast<int32_t>(std::floor(static_cast<double>(worldZ) / 32.0));
        const terrain::TerrainTile* tile = findTile(service, tileX, tileZ);
        REQUIRE(tile != nullptr);
        REQUIRE(tile->hasWeightMap());
        return terrain::sampleTileLayerWeightBilinear(tile->weightMap, layer,
                                                      static_cast<float>(worldX - tileX * 32),
                                                      static_cast<float>(worldZ - tileZ * 32), 1.0f);
    }

    [[nodiscard]] bool sameWeights(const terrain::TileWeightMapData& a, const terrain::TileWeightMapData& b)
    {
        return a.resolution == b.resolution && a.layerIndices == b.layerIndices
            && a.layerWeights == b.layerWeights;
    }

    bool saveDirty(services::EntityHandle terrainEntity)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        const entt::entity ent = services::internal::fromHandle(terrainEntity);
        REQUIRE(registry.valid(ent));
        REQUIRE(registry.all_of<components::TerrainComponent>(ent));
        return registry.get<components::TerrainComponent>(ent).saveDirty;
    }

    authoring::SculptTerrainStrokeCommand sculptCommand(authoring::SculptOp op, std::vector<glm::vec2> points,
                                                        float radius)
    {
        authoring::SculptTerrainStrokeCommand cmd;
        cmd.op = op;
        cmd.points = std::move(points);
        cmd.radius = radius;
        return cmd;
    }

    authoring::PaintTerrainLayerStrokeCommand paintCommand(authoring::PaintOp op, std::vector<glm::vec2> points,
                                                           uint32_t layer, float radius, float strength)
    {
        authoring::PaintTerrainLayerStrokeCommand cmd;
        cmd.op = op;
        cmd.points = std::move(points);
        cmd.layerIndex = layer;
        cmd.radius = radius;
        cmd.strength = strength;
        return cmd;
    }

    events::brush::ApplyBrushCommand brushDab(const glm::vec3& position, float deltaTime, bool isFirst)
    {
        events::brush::ApplyBrushCommand cmd;
        cmd.worldPosition = position;
        cmd.deltaTime = deltaTime;
        cmd.invert = false;
        cmd.isFirstApplication = isFirst;
        return cmd;
    }

    void finalizeHumanStroke()
    {
        dispatcher().execute(events::terrain::FinalizeTerrainStrokeCommand{});
    }

    void setSaveLock(bool locked)
    {
        events::terrain::SetTerrainSaveLockCommand lock;
        lock.locked = locked;
        dispatcher().execute(lock);
    }

    // A small 8-bit heightmap with relief everywhere. Every value is in [10, 200], so the luminance
    // decode stays well inside [0, 1] and no mapped height needs clamping.
    std::string writeTestHeightmap(const std::filesystem::path& dir)
    {
        procedural::HeightmapResult map;
        map.width = 9;
        map.height = 7;
        map.rgbaData.resize(static_cast<size_t>(map.width) * map.height * 4);
        for (uint32_t z = 0; z < map.height; ++z)
        {
            for (uint32_t x = 0; x < map.width; ++x)
            {
                const auto value = static_cast<uint8_t>(10 + (x * 23 + z * 17) % 191);
                const size_t texel = (static_cast<size_t>(z) * map.width + x) * 4;
                map.rgbaData[texel + 0] = value;
                map.rgbaData[texel + 1] = value;
                map.rgbaData[texel + 2] = value;
                map.rgbaData[texel + 3] = 255; // a constant alpha selects the 8-bit decode
            }
        }

        const std::string path = (dir / "authoring_apply.vfImage").string();
        REQUIRE(procedural::HeightmapGenerator::saveAsVFImage(map, path));
        return path;
    }
}

TEST_SUITE("TerrainAuthoringStroke")
{
    TEST_CASE("terrain_authoring_stroke: a one-point raise lifts the dab centre by `amount` as one undo entry")
    {
        LiveTerrain live;
        REQUIRE(live.entity.isValid());
        REQUIRE(live.service.getAllLoadedTiles().size() == 4);
        CHECK_FALSE(saveDirty(live.entity));

        const std::vector<float> pristine = wholeTerrain(live.service);

        auto cmd = sculptCommand(authoring::SculptOp::Raise, {glm::vec2(-16.0f, -16.0f)}, 5.0f);
        cmd.amount = 2.0f;
        cmd.falloff = terrain::BrushFalloff::Smooth;
        cmd.undoLabel = "MCP: Sculpt terrain (raise)";

        const services::TerrainStrokeResult result = dispatcher().execute(cmd);

        REQUIRE(result.status == Status::Ok);
        CHECK(result.message.empty());
        CHECK(result.warnings.empty());
        CHECK(result.terrainEntity == live.entity);
        CHECK(result.dabsApplied == 1);
        CHECK(result.perDabStrength == 2.0f); // a lone dab is its own peak overlap: S = 1
        CHECK_FALSE(result.closedOpenStroke);
        CHECK(result.undoPushed);
        CHECK(result.undoLabel == cmd.undoLabel);
        CHECK(result.tilesChanged == 1); // the seam neighbours were captured but did not move
        CHECK(result.tilesSkipped == 0);
        CHECK(result.footprintMin == glm::vec2(-21.0f, -21.0f));
        CHECK(result.footprintMax == glm::vec2(-11.0f, -11.0f));

        // falloff(0) == 1, so the vertex under the dab moves by exactly `amount`.
        CHECK(vertexHeight(live.service, -16, -16) == 2.0f);

        // The rim is exclusive: everything at distance >= radius is untouched, everything inside rose.
        int rimViolations = 0;
        int interiorMisses = 0;
        for (const terrain::TerrainTile* tile : live.service.getAllLoadedTiles())
        {
            for (uint32_t lz = 0; lz < 33; ++lz)
            {
                for (uint32_t lx = 0; lx < 33; ++lx)
                {
                    const float dx = static_cast<float>(tile->coord.x * 32 + static_cast<int32_t>(lx)) + 16.0f;
                    const float dz = static_cast<float>(tile->coord.z * 32 + static_cast<int32_t>(lz)) + 16.0f;
                    const float height = tile->heightData[static_cast<size_t>(lz) * 33 + lx];
                    if (dx * dx + dz * dz >= 25.0f)
                    {
                        if (height != 0.0f)
                            ++rimViolations;
                    }
                    else if (!(height > 0.0f))
                    {
                        ++interiorMisses;
                    }
                }
            }
        }
        CHECK(rimViolations == 0);
        CHECK(interiorMisses == 0);

        REQUIRE(result.samples.size() == 1);
        CHECK(result.samples[0].valid);
        CHECK(result.samples[0].xz == glm::vec2(-16.0f, -16.0f));
        CHECK(result.samples[0].before == doctest::Approx(0.0));
        CHECK(result.samples[0].after == doctest::Approx(2.0));

        CHECK(saveDirty(live.entity));

        // Exactly one entry, with the caller's label, and it round-trips bit for bit.
        REQUIRE(live.undoLog.size() == 1);
        CHECK(live.undoLog.last().getDescription() == cmd.undoLabel);

        const std::vector<float> raised = wholeTerrain(live.service);
        REQUIRE(differsSomewhere(raised, pristine));

        live.undoLog.last().undo();
        CHECK(wholeTerrain(live.service) == pristine);
        live.undoLog.last().execute();
        CHECK(wholeTerrain(live.service) == raised);
        live.undoLog.last().undo();
        CHECK(wholeTerrain(live.service) == pristine);

        // The GPU brush saw the normalised strength at deltaTime 1, never the editor's rate.
        REQUIRE_FALSE(live.gpu.dispatches.empty());
        CHECK(live.gpu.dispatches[0].brushStrength == 2.0f);
        CHECK(live.gpu.dispatches[0].deltaTime == 1.0f);
        CHECK(live.gpu.dispatches[0].brushType == terrain::BrushType::Raise);
    }

    TEST_CASE("terrain_authoring_stroke: a polyline across a seam raises its centreline by `amount`")
    {
        LiveTerrain live;
        REQUIRE(live.entity.isValid());

        // 48 m along z = -16, across the x = 0 seam. Radius 6 and spacing 0.25 put a dab every
        // 1.5 m (t = i/32, exact), and Linear is a partition of unity at s = r/4: every interior
        // centreline vertex sits under dabs summing to exactly S = 4.
        auto cmd = sculptCommand(authoring::SculptOp::Raise,
                                 {glm::vec2(-24.0f, -16.0f), glm::vec2(24.0f, -16.0f)}, 6.0f);
        cmd.amount = 3.0f;
        cmd.falloff = terrain::BrushFalloff::Linear;
        cmd.undoLabel = "MCP: Sculpt terrain (raise)";

        const services::TerrainStrokeResult result = dispatcher().execute(cmd);

        REQUIRE(result.status == Status::Ok);
        CHECK(result.dabsApplied == 33);
        CHECK(result.perDabStrength == doctest::Approx(0.75)); // 3 / 4
        CHECK(result.tilesChanged == 2);
        CHECK(result.footprintMin == glm::vec2(-30.0f, -22.0f));
        CHECK(result.footprintMax == glm::vec2(30.0f, -10.0f));

        for (int32_t x = -16; x <= 16; ++x)
        {
            CAPTURE(x);
            CHECK(vertexHeight(live.service, x, -16) == doctest::Approx(3.0).epsilon(1e-4));
        }

        // An endpoint only has the dabs on one side: 0.75 * (1 + 0.75 + 0.5 + 0.25) = 1.875, the
        // "about 62%" the MCP tool description warns about.
        CHECK(vertexHeight(live.service, -24, -16) == doctest::Approx(1.875).epsilon(1e-4));

        // Both owners of every seam vertex agree exactly.
        const terrain::TerrainTile* west = findTile(live.service, -1, -1);
        const terrain::TerrainTile* east = findTile(live.service, 0, -1);
        REQUIRE(west != nullptr);
        REQUIRE(east != nullptr);
        for (size_t lz = 0; lz < 33; ++lz)
        {
            CAPTURE(lz);
            CHECK(west->heightData[lz * 33 + 32] == east->heightData[lz * 33 + 0]);
        }

        REQUIRE(live.undoLog.size() == 1);
        CHECK(live.undoLog.last().getDescription() == cmd.undoLabel);
    }

    TEST_CASE("terrain_authoring_stroke: flatten to an explicit target and to the height under the first point")
    {
        LiveTerrain live;
        REQUIRE(live.entity.isValid());

        // A broad cone to flatten: 8 m at (-16, -16), falling off linearly over 10 m.
        auto bump = sculptCommand(authoring::SculptOp::Raise, {glm::vec2(-16.0f, -16.0f)}, 10.0f);
        bump.amount = 8.0f;
        bump.falloff = terrain::BrushFalloff::Linear;
        REQUIRE(dispatcher().execute(bump).status == Status::Ok);
        REQUIRE(live.undoLog.size() == 1);

        SUBCASE("an explicit target is reached exactly inside a constant square brush")
        {
            const float outsideBefore = vertexHeight(live.service, -13, -16);

            auto flatten = sculptCommand(authoring::SculptOp::Flatten, {glm::vec2(-16.0f, -16.0f)}, 3.0f);
            flatten.falloff = terrain::BrushFalloff::Constant;
            flatten.shape = terrain::BrushShape::Square;
            flatten.strength = 1.0f;
            flatten.targetHeight = 1.5f;

            const services::TerrainStrokeResult result = dispatcher().execute(flatten);
            REQUIRE(result.status == Status::Ok);
            CHECK(result.flattenTarget == 1.5f);
            CHECK(result.perDabStrength == 1.0f); // Flatten is never overlap-normalised

            for (int32_t z = -18; z <= -14; ++z)
            {
                for (int32_t x = -18; x <= -14; ++x)
                {
                    CAPTURE(x);
                    CAPTURE(z);
                    CHECK(vertexHeight(live.service, x, z) == 1.5f);
                }
            }

            // Chebyshev distance 3 / radius 3 = 1: on the exclusive rim, so untouched.
            CHECK(vertexHeight(live.service, -13, -16) == outsideBefore);
            CHECK(live.undoLog.size() == 2);
        }

        SUBCASE("without a target the stroke flattens to the height under points[0]")
        {
            const float expectedTarget = vertexHeight(live.service, -14, -16); // on the cone's flank
            REQUIRE(expectedTarget > 0.0f);

            auto flatten = sculptCommand(authoring::SculptOp::Flatten,
                                         {glm::vec2(-14.0f, -16.0f), glm::vec2(-18.0f, -16.0f)}, 2.0f);
            flatten.falloff = terrain::BrushFalloff::Constant;
            flatten.shape = terrain::BrushShape::Square;
            flatten.strength = 1.0f;

            const services::TerrainStrokeResult result = dispatcher().execute(flatten);
            REQUIRE(result.status == Status::Ok);
            CHECK(result.flattenTarget == doctest::Approx(expectedTarget));

            // Every vertex within Chebyshev distance 2 of some dab -- x in [-19, -13], |dz| <= 1.
            for (int32_t z = -17; z <= -15; ++z)
            {
                for (int32_t x = -19; x <= -13; ++x)
                {
                    CAPTURE(x);
                    CAPTURE(z);
                    CHECK(vertexHeight(live.service, x, z) == result.flattenTarget);
                }
            }
            CHECK(live.undoLog.size() == 2);
        }
    }

    TEST_CASE("terrain_authoring_stroke: smoothing passes pull a spike down")
    {
        LiveTerrain live;
        REQUIRE(live.entity.isValid());

        // Radius 0.5 reaches only the vertex under the dab: a one-vertex spike.
        auto spike = sculptCommand(authoring::SculptOp::Raise, {glm::vec2(-16.0f, -16.0f)}, 0.5f);
        spike.amount = 10.0f;
        spike.falloff = terrain::BrushFalloff::Constant;
        REQUIRE(dispatcher().execute(spike).status == Status::Ok);
        REQUIRE(vertexHeight(live.service, -16, -16) == 10.0f);
        REQUIRE(vertexHeight(live.service, -15, -16) == 0.0f);

        auto smooth = sculptCommand(authoring::SculptOp::Smooth, {glm::vec2(-16.0f, -16.0f)}, 3.0f);
        smooth.strength = 1.0f;
        smooth.passes = 3;
        const services::TerrainStrokeResult result = dispatcher().execute(smooth);

        REQUIRE(result.status == Status::Ok);
        CHECK(result.dabsApplied == 3); // one dab, three passes
        CHECK(vertexHeight(live.service, -16, -16) < 10.0f);
        CHECK(vertexHeight(live.service, -15, -16) > 0.0f); // the spike spread into its neighbours
        CHECK(live.undoLog.size() == 2);
    }

    TEST_CASE("terrain_authoring_stroke: lowering clamps at the terrain's minHeight")
    {
        LiveTerrain live;
        REQUIRE(live.entity.isValid());

        // 50 m down is inside the 110 m height range, far past minHeight = -10.
        auto lower = sculptCommand(authoring::SculptOp::Lower, {glm::vec2(-16.0f, -16.0f)}, 4.0f);
        lower.amount = 50.0f;
        lower.falloff = terrain::BrushFalloff::Smooth;
        const services::TerrainStrokeResult result = dispatcher().execute(lower);

        REQUIRE(result.status == Status::Ok);
        CHECK(vertexHeight(live.service, -16, -16) == -10.0f);
        CHECK(vertexHeight(live.service, -15, -16) == -10.0f); // 50 * smooth(1/4) = 42.2 -> clamped
        // smooth(3/4) = 0.15625, exact: 50 * 0.15625 = 7.8125 does not reach the floor.
        CHECK(vertexHeight(live.service, -13, -16) == doctest::Approx(-7.8125));

        // No label given: the stroke keeps the editor's.
        CHECK(result.undoLabel == "Sculpt Terrain");
        REQUIRE(live.undoLog.size() == 1);
        CHECK(live.undoLog.last().getDescription() == "Sculpt Terrain");
    }

    TEST_CASE("terrain_authoring_stroke: a stroke partly off the terrain reports what it skipped")
    {
        LiveTerrain live;
        REQUIRE(live.entity.isValid());

        // Radius 6 at x = -30 reaches x = -36: tile x = -2 does not exist.
        auto raise = sculptCommand(authoring::SculptOp::Raise, {glm::vec2(-30.0f, -16.0f)}, 6.0f);
        raise.amount = 1.0f;
        const services::TerrainStrokeResult result = dispatcher().execute(raise);

        REQUIRE(result.status == Status::Ok);
        CHECK(result.tilesSkipped == 1);
        REQUIRE(result.warnings.size() == 1);
        CHECK(result.warnings[0].find("outside the terrain") != std::string::npos);
        CHECK(vertexHeight(live.service, -30, -16) == 1.0f);
    }

    TEST_CASE("terrain_authoring_stroke: every refusal mutates nothing and pushes nothing")
    {
        LiveTerrain live;
        REQUIRE(live.entity.isValid());

        const std::vector<float> pristine = wholeTerrain(live.service);

        auto raise = sculptCommand(authoring::SculptOp::Raise, {glm::vec2(-16.0f, -16.0f)}, 4.0f);
        raise.amount = 2.0f;

        Status expected = Status::Ok;
        services::TerrainStrokeResult result;

        SUBCASE("SaveInProgress")
        {
            setSaveLock(true);
            result = dispatcher().execute(raise);
            setSaveLock(false);
            expected = Status::SaveInProgress;
        }
        SUBCASE("GpuUnavailable")
        {
            live.service.setBrushComputeProvider(nullptr);
            result = dispatcher().execute(raise);
            expected = Status::GpuUnavailable;
        }
        SUBCASE("NoTerrain: the named entity is not a terrain")
        {
            raise.terrainEntity = services::EntityHandle{987654};
            result = dispatcher().execute(raise);
            expected = Status::NoTerrain;
        }
        SUBCASE("OffTerrain")
        {
            raise.points = {glm::vec2(1000.0f, 1000.0f)};
            result = dispatcher().execute(raise);
            expected = Status::OffTerrain;
        }
        SUBCASE("TooMuchWork: a footprint over the GPU tile budget")
        {
            raise.radius = 512.0f; // 33 x 33 tiles of 32 m for a single dab
            result = dispatcher().execute(raise);
            expected = Status::TooMuchWork;
        }
        SUBCASE("TooMuchWork: too many points")
        {
            raise.points.assign(authoring::MAX_STROKE_POINTS + 1, glm::vec2(-16.0f, -16.0f));
            result = dispatcher().execute(raise);
            expected = Status::TooMuchWork;
        }
        SUBCASE("TooMuchWork: smooth passes times dabs")
        {
            // 81 dabs (20 m at 0.25 m spacing) x 64 passes, far past 1024.
            auto smooth = sculptCommand(authoring::SculptOp::Smooth,
                                        {glm::vec2(-26.0f, -16.0f), glm::vec2(-6.0f, -16.0f)}, 1.0f);
            smooth.passes = authoring::MAX_SMOOTH_PASSES;
            result = dispatcher().execute(smooth);
            expected = Status::TooMuchWork;
        }
        SUBCASE("NoHeightAtPoint: the first point of a target-less flatten is off the terrain")
        {
            // x = 40 is tile 1, which does not exist; the stroke still reaches tile 0 at x = 28.
            auto flatten = sculptCommand(authoring::SculptOp::Flatten,
                                         {glm::vec2(40.0f, -16.0f), glm::vec2(28.0f, -16.0f)}, 4.0f);
            result = dispatcher().execute(flatten);
            expected = Status::NoHeightAtPoint;
        }
        SUBCASE("InvalidArguments: radius below the minimum")
        {
            raise.radius = 0.05f;
            result = dispatcher().execute(raise);
            expected = Status::InvalidArguments;
        }
        SUBCASE("InvalidArguments: a non-finite point")
        {
            raise.points = {glm::vec2(std::numeric_limits<float>::quiet_NaN(), 0.0f)};
            result = dispatcher().execute(raise);
            expected = Status::InvalidArguments;
        }
        SUBCASE("InvalidArguments: no points")
        {
            raise.points.clear();
            result = dispatcher().execute(raise);
            expected = Status::InvalidArguments;
        }
        SUBCASE("InvalidArguments: a zero amount")
        {
            raise.amount = 0.0f;
            result = dispatcher().execute(raise);
            expected = Status::InvalidArguments;
        }
        SUBCASE("InvalidArguments: an amount beyond the terrain's height range")
        {
            raise.amount = 200.0f; // the range is 110 m
            result = dispatcher().execute(raise);
            expected = Status::InvalidArguments;
        }
        SUBCASE("InvalidArguments: spacing below the minimum")
        {
            raise.spacing = 0.01f;
            result = dispatcher().execute(raise);
            expected = Status::InvalidArguments;
        }
        SUBCASE("InvalidArguments: flatten strength above 1")
        {
            auto flatten = sculptCommand(authoring::SculptOp::Flatten, {glm::vec2(-16.0f, -16.0f)}, 4.0f);
            flatten.strength = 1.5f;
            result = dispatcher().execute(flatten);
            expected = Status::InvalidArguments;
        }
        SUBCASE("InvalidArguments: a flatten target outside the height range")
        {
            auto flatten = sculptCommand(authoring::SculptOp::Flatten, {glm::vec2(-16.0f, -16.0f)}, 4.0f);
            flatten.targetHeight = 500.0f;
            result = dispatcher().execute(flatten);
            expected = Status::InvalidArguments;
        }
        SUBCASE("InvalidArguments: zero smooth passes")
        {
            auto smooth = sculptCommand(authoring::SculptOp::Smooth, {glm::vec2(-16.0f, -16.0f)}, 4.0f);
            smooth.passes = 0;
            result = dispatcher().execute(smooth);
            expected = Status::InvalidArguments;
        }
        SUBCASE("InvalidArguments: a paint layer past the palette")
        {
            auto paint = paintCommand(authoring::PaintOp::Paint, {glm::vec2(-16.0f, -16.0f)},
                                      static_cast<uint32_t>(terrain::MAX_TERRAIN_LAYERS), 4.0f, 0.5f);
            result = dispatcher().execute(paint);
            expected = Status::InvalidArguments;
        }
        SUBCASE("OffTerrain: a paint stroke nowhere near the terrain")
        {
            auto paint = paintCommand(authoring::PaintOp::Paint, {glm::vec2(500.0f, 500.0f)}, 1, 4.0f, 0.5f);
            result = dispatcher().execute(paint);
            expected = Status::OffTerrain;
        }

        CHECK(result.status == expected);
        CHECK_FALSE(result.message.empty());
        CHECK_FALSE(result.undoPushed);
        CHECK(wholeTerrain(live.service) == pristine);
        CHECK(live.undoLog.empty());
        CHECK(live.gpu.dispatches.empty());
        CHECK_FALSE(saveDirty(live.entity));
    }

    TEST_CASE("terrain_authoring_stroke: with no terrain every command refuses with NoTerrain")
    {
        LiveTerrain live(/*withTerrain=*/false);

        auto raise = sculptCommand(authoring::SculptOp::Raise, {glm::vec2(0.0f, 0.0f)}, 4.0f);
        const services::TerrainStrokeResult sculpted = dispatcher().execute(raise);
        CHECK(sculpted.status == Status::NoTerrain);
        CHECK_FALSE(sculpted.message.empty());

        const auto painted = dispatcher().execute(
            paintCommand(authoring::PaintOp::Paint, {glm::vec2(0.0f, 0.0f)}, 1, 4.0f, 0.5f));
        CHECK(painted.status == Status::NoTerrain);

        authoring::ApplyHeightmapCommand apply;
        apply.heightmapPath = "C:/does/not/matter.vfImage";
        CHECK(dispatcher().execute(apply).status == Status::NoTerrain);

        CHECK(live.undoLog.empty());
    }

    TEST_CASE("terrain_authoring_stroke: an open human stroke is pushed as its own entry first")
    {
        LiveTerrain live;
        REQUIRE(live.entity.isValid());

        SculptModeFake mode(live.entity);
        mode.type = terrain::BrushType::Raise;
        mode.params.radius = 4.0f;
        mode.params.strength = 3.0f;

        // The human presses the mouse: one dab lands and the stroke stays open.
        dispatcher().execute(brushDab(glm::vec3(16.0f, 0.0f, 16.0f), 0.5f, /*isFirst=*/true));
        REQUIRE(live.undoLog.empty());
        CHECK(vertexHeight(live.service, 16, 16) == 1.5f); // 3 * 0.5 at the centre

        auto mcp = sculptCommand(authoring::SculptOp::Raise, {glm::vec2(-16.0f, -16.0f)}, 4.0f);
        mcp.amount = 2.0f;
        mcp.undoLabel = "MCP: Sculpt terrain (raise)";
        const services::TerrainStrokeResult result = dispatcher().execute(mcp);

        REQUIRE(result.status == Status::Ok);
        CHECK(result.closedOpenStroke);
        REQUIRE(live.undoLog.size() == 2);
        CHECK(live.undoLog.pushed[0]->getDescription() == "Sculpt Terrain");
        CHECK(live.undoLog.pushed[1]->getDescription() == "MCP: Sculpt terrain (raise)");

        // The human keeps dragging: the next dab opens a fresh stroke under the editor's label.
        dispatcher().execute(brushDab(glm::vec3(17.0f, 0.0f, 16.0f), 0.5f, /*isFirst=*/false));
        finalizeHumanStroke();
        REQUIRE(live.undoLog.size() == 3);
        CHECK(live.undoLog.pushed[2]->getDescription() == "Sculpt Terrain");

        // Each entry owns only its own edit.
        live.undoLog.pushed[1]->undo();
        CHECK(vertexHeight(live.service, -16, -16) == 0.0f);
        CHECK(vertexHeight(live.service, 16, 16) > 0.0f);
    }

    TEST_CASE("terrain_authoring_stroke: the editor brush wrappers behave exactly as before")
    {
        LiveTerrain live;
        REQUIRE(live.entity.isValid());

        SUBCASE("Flatten captures its target on the first dab and reuses it")
        {
            SculptModeFake mode(live.entity);
            mode.type = terrain::BrushType::Flatten;
            mode.params.radius = 3.0f;
            mode.params.strength = 10.0f; // the editor's raw rate: never normalised, never clamped to 1
            mode.params.falloff = terrain::BrushFalloff::Linear;

            dispatcher().execute(brushDab(glm::vec3(-16.0f, 3.5f, -16.0f), 0.25f, /*isFirst=*/true));
            dispatcher().execute(brushDab(glm::vec3(-15.0f, 7.0f, -16.0f), 0.25f, /*isFirst=*/false));

            REQUIRE(live.gpu.dispatches.size() == 2); // each dab's footprint is tile (-1, -1) only
            CHECK(live.gpu.dispatches[0].targetHeight == 3.5f);
            CHECK(live.gpu.dispatches[1].targetHeight == 3.5f); // the second hit's 7.0 is ignored

            const terrain::BrushGPUParams& p = live.gpu.dispatches[0];
            CHECK(p.brushCenter == glm::vec2(-16.0f, -16.0f));
            CHECK(p.tileWorldOrigin == glm::vec2(-32.0f, -32.0f));
            CHECK(p.brushRadius == 3.0f);
            CHECK(p.brushStrength == 10.0f);
            CHECK(p.vertexSpacing == 1.0f);
            CHECK(p.verticesPerSide == 33u);
            CHECK(p.falloff == terrain::BrushFalloff::Linear);
            CHECK(p.shape == terrain::BrushShape::Circle);
            CHECK(p.brushType == terrain::BrushType::Flatten);
            CHECK(p.deltaTime == 0.25f);
            CHECK(p.minHeight == -10.0f);
            CHECK(p.maxHeight == 100.0f);
            CHECK_FALSE(p.invert);
            CHECK(p.stampRotation == mode.params.stampRotation);
            CHECK(p.stampScale == mode.params.stampScale);
            CHECK(p.stampWidth == 0u);
            CHECK(p.stampHeight == 0u);
            CHECK(p.talusAngle == mode.params.talusAngle);
            CHECK(p.terraceStepHeight == mode.params.terraceStepHeight);
            CHECK(p.terraceSharpness == mode.params.terraceSharpness);

            CHECK(live.gpu.dispatches[1].brushCenter == glm::vec2(-15.0f, -16.0f));

            finalizeHumanStroke();
            REQUIRE(live.undoLog.size() == 1);
            CHECK(live.undoLog.last().getDescription() == "Sculpt Terrain");
            CHECK(saveDirty(live.entity));
        }

        SUBCASE("Stamp is one-shot per click and carries the stamp size")
        {
            SculptModeFake mode(live.entity);
            mode.type = terrain::BrushType::Stamp;
            mode.params.radius = 4.0f;
            mode.params.stampSubtract = true;
            auto stamp = std::make_shared<terrain::HeightmapData>();
            stamp->width = 4;
            stamp->height = 3;
            stamp->heights.assign(12, 0.5f);
            mode.stamp = stamp;

            dispatcher().execute(brushDab(glm::vec3(-16.0f, 0.0f, -16.0f), 0.1f, /*isFirst=*/false));
            CHECK(live.gpu.dispatches.empty()); // a drag never stamps
            finalizeHumanStroke();
            CHECK(live.undoLog.empty());        // ... and never opened a stroke

            dispatcher().execute(brushDab(glm::vec3(-16.0f, 0.0f, -16.0f), 0.1f, /*isFirst=*/true));
            REQUIRE(live.gpu.dispatches.size() == 1);
            CHECK(live.gpu.dispatches[0].brushType == terrain::BrushType::Stamp);
            CHECK(live.gpu.dispatches[0].stampWidth == 4u);
            CHECK(live.gpu.dispatches[0].stampHeight == 3u);
            CHECK(live.gpu.dispatches[0].invert); // invert (false) XOR stampSubtract (true)
        }

        SUBCASE("Ramp applies on the second click as one 'Ramp Terrain' entry")
        {
            SculptModeFake mode(live.entity);
            mode.type = terrain::BrushType::Ramp;
            mode.params.rampWidth = 4.0f;
            mode.params.rampFalloff = 2.0f;

            dispatcher().execute(brushDab(glm::vec3(-24.0f, 0.0f, -16.0f), 0.1f, /*isFirst=*/true));
            CHECK(live.undoLog.empty()); // the first click only captures the start

            dispatcher().execute(brushDab(glm::vec3(24.0f, 6.0f, -16.0f), 0.1f, /*isFirst=*/true));
            REQUIRE(live.undoLog.size() == 1);
            CHECK(live.undoLog.last().getDescription() == "Ramp Terrain");
            CHECK(live.gpu.dispatches.empty()); // the ramp is the one CPU sculpt brush
            CHECK(vertexHeight(live.service, 0, -16) == doctest::Approx(3.0)); // halfway up
        }

        SUBCASE("Layer paint keeps its rate, its eviction and its 'Paint Terrain' entry")
        {
            PaintModeFake mode(live.entity);
            mode.type = terrain::PaintBrushType::PaintLayer;
            mode.params.radius = 4.0f;
            mode.params.strength = 2.0f;
            mode.params.opacity = 1.0f;
            mode.params.activeLayer = 1;

            events::paintBrush::ApplyPaintBrushCommand dab;
            dab.worldPosition = glm::vec3(-16.0f, 0.0f, -16.0f);
            dab.deltaTime = 0.1f;
            dab.isFirstApplication = true;
            dispatcher().execute(dab);
            finalizeHumanStroke();

            CHECK(layerWeight(live.service, -16, -16, 1) == doctest::Approx(0.2)); // 2 * 1 * 0.1
            REQUIRE(live.undoLog.size() == 1);
            CHECK(live.undoLog.last().getDescription() == "Paint Terrain");
            CHECK(saveDirty(live.entity));
        }
    }

    TEST_CASE("terrain_authoring_stroke: painting a layer adds `strength` at the centre as one entry")
    {
        LiveTerrain live(/*withTerrain=*/true, /*withGpu=*/false); // paint is CPU-only
        REQUIRE(live.entity.isValid());

        terrain::TerrainTile* tile = findTile(live.service, -1, -1);
        REQUIRE(tile != nullptr);
        REQUIRE(tile->hasWeightMap());
        const terrain::TileWeightMapData before = tile->weightMap;

        auto paint = paintCommand(authoring::PaintOp::Paint, {glm::vec2(-16.0f, -16.0f)}, 1, 4.0f, 0.6f);
        paint.undoLabel = "MCP: Paint terrain layer (1)";
        const services::TerrainStrokeResult result = dispatcher().execute(paint);

        REQUIRE(result.status == Status::Ok);
        CHECK(result.perDabStrength == doctest::Approx(0.6));
        CHECK(result.undoPushed);
        CHECK(result.tilesChanged == 1);
        CHECK(layerWeight(live.service, -16, -16, 1) == doctest::Approx(0.6).epsilon(1e-5));
        CHECK(layerWeight(live.service, -16, -16, 0) == doctest::Approx(0.4).epsilon(1e-5)); // renormalised

        REQUIRE(result.samples.size() == 1);
        CHECK(result.samples[0].valid);
        CHECK(result.samples[0].before == doctest::Approx(0.0));
        CHECK(result.samples[0].after == doctest::Approx(0.6).epsilon(1e-5));

        REQUIRE(live.undoLog.size() == 1);
        CHECK(live.undoLog.last().getDescription() == paint.undoLabel);

        live.undoLog.last().undo();
        CHECK(sameWeights(findTile(live.service, -1, -1)->weightMap, before));

        SUBCASE("erase takes the same amount back off")
        {
            live.undoLog.last().execute(); // redo the paint
            auto erase = paintCommand(authoring::PaintOp::Erase, {glm::vec2(-16.0f, -16.0f)}, 1, 4.0f, 0.5f);
            REQUIRE(dispatcher().execute(erase).status == Status::Ok);
            CHECK(layerWeight(live.service, -16, -16, 1) == doctest::Approx(0.1).epsilon(1e-5));
            CHECK(live.undoLog.size() == 2);
        }

        SUBCASE("erasing a layer the tile does not carry changes nothing, even with eviction allowed")
        {
            auto erase = paintCommand(authoring::PaintOp::Erase, {glm::vec2(-16.0f, -16.0f)}, 12, 4.0f, 0.5f);
            erase.allowChannelEviction = true;
            const services::TerrainStrokeResult erased = dispatcher().execute(erase);
            REQUIRE(erased.status == Status::Ok);
            CHECK_FALSE(erased.undoPushed);
            CHECK(erased.tilesSkipped == 0);
            CHECK(erased.warnings.empty());
            CHECK(sameWeights(findTile(live.service, -1, -1)->weightMap, before));
            CHECK(live.undoLog.size() == 1);
        }
    }

    TEST_CASE("terrain_authoring_stroke: a 9th layer is refused by default and evicts only when allowed")
    {
        LiveTerrain live(/*withTerrain=*/true, /*withGpu=*/false);
        REQUIRE(live.entity.isValid());

        // Occupy all 8 weight channels of tile (-1, -1): texel i belongs wholly to channel i % 8.
        terrain::TerrainTile* tile = findTile(live.service, -1, -1);
        REQUIRE(tile != nullptr);
        REQUIRE(tile->hasWeightMap());
        terrain::TileWeightMapData& weights = tile->weightMap;
        REQUIRE(weights.layerWeights.size() == terrain::WEIGHT_CHANNELS);
        for (auto& channel : weights.layerWeights)
            std::fill(channel.begin(), channel.end(), 0.0f);
        for (size_t texel = 0; texel < weights.getTexelCount(); ++texel)
            weights.layerWeights[texel % terrain::WEIGHT_CHANNELS][texel] = 1.0f;
        const terrain::TileWeightMapData before = weights;

        // Palette layer 9 has no channel and none is free. Radius 3 stays inside tile (-1, -1).
        auto paint = paintCommand(authoring::PaintOp::Paint, {glm::vec2(-16.0f, -16.0f)}, 9, 3.0f, 0.5f);

        SUBCASE("refused: the tile is skipped, untouched and reported")
        {
            const services::TerrainStrokeResult result = dispatcher().execute(paint);
            REQUIRE(result.status == Status::Ok);
            CHECK_FALSE(result.undoPushed);
            CHECK(result.tilesChanged == 0);
            CHECK(result.tilesSkipped == 1);
            REQUIRE(result.warnings.size() == 1);
            CHECK(result.warnings[0].find("allowChannelEviction") != std::string::npos);
            CHECK(sameWeights(findTile(live.service, -1, -1)->weightMap, before));
            CHECK(live.undoLog.empty());
        }

        SUBCASE("allowed: a channel is evicted, and undo restores the palette")
        {
            paint.allowChannelEviction = true;
            const services::TerrainStrokeResult result = dispatcher().execute(paint);
            REQUIRE(result.status == Status::Ok);
            CHECK(result.undoPushed);
            CHECK(result.tilesSkipped == 0);

            const auto& indices = findTile(live.service, -1, -1)->weightMap.layerIndices;
            CHECK(std::find(indices.begin(), indices.end(), static_cast<uint8_t>(9)) != indices.end());

            REQUIRE(live.undoLog.size() == 1);
            live.undoLog.last().undo();
            CHECK(sameWeights(findTile(live.service, -1, -1)->weightMap, before));
        }
    }

    TEST_CASE("terrain_authoring_stroke: applying a heightmap equals creating the terrain with it")
    {
        TempDir temp("vf_vk1653_authoring_stroke");
        const std::string heightmapPath = writeTestHeightmap(temp.path);

        LiveTerrain live(/*withTerrain=*/false, /*withGpu=*/false); // the apply is CPU-only

        // Reference: the same 2x2 terrain created WITH the heightmap, mapped onto [-10, 100].
        std::vector<float> expected;
        {
            services::TerrainCreationData withMap = quadTerrain();
            withMap.heightmapPath = heightmapPath;
            const services::EntityHandle created = live.service.createTerrain(withMap);
            REQUIRE(created.isValid());
            ScopedTerrain guard(live.service, created);
            expected = wholeTerrain(live.service);
        }
        REQUIRE(expected.size() == 4u * 33u * 33u);
        REQUIRE(std::any_of(expected.begin(), expected.end(), [](float h) { return h != 0.0f; }));

        // Then a flat one, with the same heightmap applied over the full [-10, 100] range.
        const services::EntityHandle flat = live.service.createTerrain(quadTerrain());
        REQUIRE(flat.isValid());
        ScopedTerrain flatGuard(live.service, flat);
        const std::vector<float> pristine = wholeTerrain(live.service);

        authoring::ApplyHeightmapCommand apply;
        apply.heightmapPath = heightmapPath;
        apply.baseHeight = -10.0f;
        apply.amplitude = 110.0f; // -10 + 110 == 100 exactly, so the two mappings are identical
        apply.undoLabel = "MCP: Apply heightmap (test, seed 0)";
        const services::TerrainStrokeResult result = dispatcher().execute(apply);

        REQUIRE(result.status == Status::Ok);
        CHECK(result.terrainEntity == flat);
        CHECK(result.tilesChanged == 4);
        CHECK(result.tilesSkipped == 0);
        CHECK(result.undoPushed);
        CHECK(result.undoLabel == apply.undoLabel);
        CHECK(result.footprintMin == glm::vec2(-32.0f, -32.0f));
        CHECK(result.footprintMax == glm::vec2(32.0f, 32.0f));
        CHECK(saveDirty(flat));

        const std::vector<float> applied = wholeTerrain(live.service);
        CHECK(applied == expected); // bit for bit

        REQUIRE(live.undoLog.size() == 1);
        CHECK(live.undoLog.last().getDescription() == apply.undoLabel);

        live.undoLog.last().undo();
        CHECK(wholeTerrain(live.service) == pristine);
        live.undoLog.last().execute();
        CHECK(wholeTerrain(live.service) == applied);
    }

    TEST_CASE("terrain_authoring_stroke: a heightmap apply refuses before touching anything")
    {
        TempDir temp("vf_vk1653_authoring_apply_refusals");
        const std::string heightmapPath = writeTestHeightmap(temp.path);

        LiveTerrain live(/*withTerrain=*/true, /*withGpu=*/false);
        REQUIRE(live.entity.isValid());
        const std::vector<float> pristine = wholeTerrain(live.service);

        authoring::ApplyHeightmapCommand apply;
        apply.heightmapPath = heightmapPath;

        Status expected = Status::Ok;
        services::TerrainStrokeResult result;

        SUBCASE("a missing file")
        {
            apply.heightmapPath = (temp.path / "missing.vfImage").string();
            result = dispatcher().execute(apply);
            expected = Status::InvalidArguments;
        }
        SUBCASE("a zero amplitude")
        {
            apply.amplitude = 0.0f;
            result = dispatcher().execute(apply);
            expected = Status::InvalidArguments;
        }
        SUBCASE("a non-finite base height")
        {
            apply.baseHeight = std::numeric_limits<float>::infinity();
            result = dispatcher().execute(apply);
            expected = Status::InvalidArguments;
        }
        SUBCASE("a save in progress")
        {
            setSaveLock(true);
            result = dispatcher().execute(apply);
            setSaveLock(false);
            expected = Status::SaveInProgress;
        }

        CHECK(result.status == expected);
        CHECK_FALSE(result.message.empty());
        CHECK(wholeTerrain(live.service) == pristine);
        CHECK(live.undoLog.empty());
        CHECK_FALSE(saveDirty(live.entity));
    }
}
