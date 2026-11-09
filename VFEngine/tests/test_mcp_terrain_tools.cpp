#include <doctest.h>

#include "dispatch/MainThreadQueue.hpp"
#include "protocol/ArgReader.hpp"
#include "protocol/ToolRegistry.hpp"
#include "tools/CoreTools.hpp"
#include "tools/TerrainToolSupport.hpp"

#include "events/EventDispatcher.hpp"
#include "events/editor/EditorModeEvents.hpp"
#include "events/editor/UndoRedoEvents.hpp"
#include "events/physics/PhysicsEvents.hpp"
#include "events/project/ProjectEvents.hpp"
#include "events/scene/EntityTransformEvents.hpp"
#include "events/scene/ScenePersistenceEvents.hpp"
#include "events/scripting/ScriptingEvents.hpp"
#include "events/terrain/HeightmapGenerationEvents.hpp"
#include "events/terrain/TerrainAuthoringEvents.hpp"
#include "events/terrain/TerrainEvents.hpp"
#include "events/terrain/TerrainMaterialAssetEvents.hpp"
#include "events/world/WorldSectorEvents.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

// VK-1653: the MCP terrain tools against a fake terrain service. Every test also checks that no
// MCP code pushed undo itself or opened an undo batch: terrain undo belongs to TerrainService.
namespace
{
    namespace fs = std::filesystem;
    namespace authoring = events::terrainAuthoring;

    constexpr uint64_t terrainId = 5;
    constexpr uint64_t otherTerrainId = 9;
    constexpr uint64_t tileId = 51;
    constexpr uint64_t createdTerrainId = 40;

    struct DispatcherScope
    {
        DispatcherScope()
        {
            events::EventDispatcher::instance().clear();
        }

        ~DispatcherScope()
        {
            events::EventDispatcher::instance().clear();
        }
    };

    fs::path makeTestDirectory(const char* name)
    {
        auto directory = fs::temp_directory_path() / name;
        fs::remove_all(directory);
        fs::create_directories(directory);
        return fs::weakly_canonical(directory);
    }

    void touch(const fs::path& path)
    {
        fs::create_directories(path.parent_path());
        std::ofstream stream(path, std::ios::binary);
        stream << "fake";
    }

    std::string materialKey(const std::string& path)
    {
        return fs::path(path).lexically_normal().generic_string();
    }

    services::EntityHandle handleOf(uint64_t id)
    {
        services::EntityHandle handle;
        handle.id = id;
        return handle;
    }

    bool contains(const std::string& text, const std::string& needle)
    {
        return text.find(needle) != std::string::npos;
    }

    // The message of the exception `call` throws; empty when it throws nothing.
    std::string thrownMessage(const std::function<void()>& call)
    {
        try
        {
            call();
        }
        catch (const std::exception& e)
        {
            return e.what();
        }
        return {};
    }

    // A live 4 x 4 medium terrain centred on the origin: X and Z span -64..64, 0.5 m vertex spacing.
    services::TerrainSummary terrainSummary(uint64_t id, std::string name, bool live = true)
    {
        services::TerrainSummary summary;
        summary.entity = handleOf(id);
        summary.name = std::move(name);
        summary.live = live;
        summary.residentHeightTiles = 16;
        summary.data.resolution = 1;
        summary.data.worldTileSize = 32.0f;
        summary.data.minHeight = -10.0f;
        summary.data.maxHeight = 100.0f;
        summary.data.gridMinX = -2;
        summary.data.gridMinZ = -2;
        summary.data.gridMaxX = 1;
        summary.data.gridMaxZ = 1;
        summary.data.tileCount = 16;
        return summary;
    }

    struct FakeTerrainEngine
    {
        fs::path assets;
        bool playMode = false;
        bool worldMode = false;
        bool saveLocked = false;
        bool creationPending = false;
        bool saveSucceeds = true;
        bool colliderSucceeds = true;
        bool beginAccepted = true;

        std::vector<services::TerrainSummary> terrains;
        std::map<uint64_t, services::EntityData> entities;
        std::map<std::string, services::TerrainMaterialInfo> materials; // by materialKey

        std::vector<std::string> log; // mutating events, in dispatch order
        int undoPushes = 0;
        int batchCommands = 0;

        services::TerrainStrokeResult strokeResult;
        std::vector<authoring::SculptTerrainStrokeCommand> sculpts;
        std::vector<authoring::PaintTerrainLayerStrokeCommand> paints;
        std::vector<authoring::ApplyHeightmapCommand> applies;
        std::vector<authoring::GetTerrainHeightsQuery> heightQueries;
        std::vector<std::vector<services::TerrainSaveRequest>> saves;
        std::optional<services::TerrainCreationData> createdConfig;
        std::vector<std::string> materialAssignments;
        std::vector<events::terrainMaterial::CreateTerrainMaterialAssetCommand> materialCreates;
        std::vector<events::terrainMaterial::EditTerrainMaterialLayerCommand> layerEdits;
        services::HeightmapProbeResult probe;
        std::string scenePath;

        std::vector<services::HeightmapJobState> pollStates{services::HeightmapJobState::Done};
        std::optional<services::HeightmapGenerationRequest> begun;
        int polls = 0;

        FakeTerrainEngine()
        {
            strokeResult.dabsApplied = 3;
            strokeResult.tilesChanged = 4;
            strokeResult.footprintMin = {-1.0f, -2.0f};
            strokeResult.footprintMax = {3.0f, 4.0f};
            strokeResult.flattenTarget = 2.5f;
            services::TerrainStrokeSample sample;
            sample.xz = {1.0f, 2.0f};
            sample.before = 0.5f;
            sample.after = 1.5f;
            sample.valid = true;
            strokeResult.samples.push_back(sample);

            probe.valid = true;
            probe.width = 512;
            probe.height = 512;
        }

        services::TerrainSummary* findTerrain(const services::EntityHandle& handle)
        {
            for (services::TerrainSummary& terrain : terrains)
            {
                if (terrain.entity == handle)
                {
                    return &terrain;
                }
            }
            return nullptr;
        }

        services::TerrainStrokeResult strokeOutcome(const services::EntityHandle& terrain, const std::string& label) const
        {
            services::TerrainStrokeResult result = strokeResult;
            result.terrainEntity = terrain;
            if (result.status == services::TerrainStrokeStatus::Ok)
            {
                result.undoPushed = true;
                result.undoLabel = label;
            }
            else
            {
                result.tilesChanged = 0;
                result.message = "engine refused";
            }
            return result;
        }

        void addMaterial(const std::string& path, const std::vector<std::string>& layerNames)
        {
            services::TerrainMaterialInfo info;
            info.path = path;
            info.name = "Ground";
            info.maxLayers = 32;
            info.activeLayerCount = static_cast<uint32_t>(layerNames.size());
            for (const std::string& name : layerNames)
            {
                services::TerrainMaterialLayerInfo layer;
                layer.name = name;
                info.layers.push_back(layer);
            }
            materials[materialKey(path)] = info;
        }

        void registerHandlers()
        {
            auto& d = events::EventDispatcher::instance();

            d.registerQueryHandler<events::editor::IsPlayModeQuery>(
                [this](const events::editor::IsPlayModeQuery&) { return playMode; });
            d.registerQueryHandler<events::world::IsWorldModeQuery>(
                [this](const events::world::IsWorldModeQuery&) { return worldMode; });
            d.registerQueryHandler<authoring::IsTerrainSaveLockedQuery>(
                [this](const authoring::IsTerrainSaveLockedQuery&) { return saveLocked; });
            d.registerQueryHandler<authoring::IsTerrainCreationPendingQuery>(
                [this](const authoring::IsTerrainCreationPendingQuery&) { return creationPending; });
            d.registerQueryHandler<events::project::GetCurrentProjectQuery>(
                [this](const events::project::GetCurrentProjectQuery&) -> std::optional<config::ProjectConfig>
                {
                    config::ProjectConfig config;
                    config.projectName = "McpTerrain";
                    config.workingDirectory = assets.string();
                    return config;
                });
            d.registerQueryHandler<authoring::ListTerrainsQuery>(
                [this](const authoring::ListTerrainsQuery&) { return terrains; });
            d.registerQueryHandler<events::scene::GetEntityQuery>(
                [this](const events::scene::GetEntityQuery& query) -> std::optional<services::EntityData>
                {
                    auto it = entities.find(query.entity.id);
                    if (it == entities.end())
                    {
                        return std::nullopt;
                    }
                    return it->second;
                });

            d.registerCommandHandler<authoring::SculptTerrainStrokeCommand>(
                [this](const authoring::SculptTerrainStrokeCommand& command)
                {
                    log.push_back("SculptTerrainStroke");
                    sculpts.push_back(command);
                    return strokeOutcome(command.terrainEntity, command.undoLabel);
                });
            d.registerCommandHandler<authoring::PaintTerrainLayerStrokeCommand>(
                [this](const authoring::PaintTerrainLayerStrokeCommand& command)
                {
                    log.push_back("PaintTerrainLayerStroke");
                    paints.push_back(command);
                    return strokeOutcome(command.terrainEntity, command.undoLabel);
                });
            d.registerCommandHandler<authoring::ApplyHeightmapCommand>(
                [this](const authoring::ApplyHeightmapCommand& command)
                {
                    log.push_back("ApplyTerrainHeightmap");
                    applies.push_back(command);
                    return strokeOutcome(command.terrainEntity, command.undoLabel);
                });

            // A slope: h = 0.5 * x over X, Z in [-64, 64).
            d.registerQueryHandler<authoring::GetTerrainHeightsQuery>(
                [this](const authoring::GetTerrainHeightsQuery& query)
                {
                    heightQueries.push_back(query);
                    std::vector<services::TerrainHeightSample> samples;
                    for (const glm::vec2& position : query.positions)
                    {
                        services::TerrainHeightSample sample;
                        sample.onTerrain = position.x >= -64.0f && position.x < 64.0f &&
                            position.y >= -64.0f && position.y < 64.0f;
                        sample.valid = sample.onTerrain;
                        sample.height = sample.valid ? 0.5f * position.x : 0.0f;
                        samples.push_back(sample);
                    }
                    return samples;
                });

            d.registerCommandHandler<authoring::SaveTerrainsCommand>(
                [this](const authoring::SaveTerrainsCommand& command)
                {
                    log.push_back("SaveTerrains");
                    saves.push_back(command.requests);
                    std::vector<services::TerrainSaveOutcome> outcomes;
                    for (const services::TerrainSaveRequest& request : command.requests)
                    {
                        services::TerrainSaveOutcome outcome;
                        outcome.terrainEntity = request.terrainEntity;
                        outcome.path = request.path;
                        outcome.success = saveSucceeds;
                        if (saveSucceeds)
                        {
                            touch(fs::path(request.path));
                            if (services::TerrainSummary* terrain = findTerrain(request.terrainEntity))
                            {
                                terrain->data.savePath = request.path;
                                terrain->data.saveDirty = false;
                            }
                        }
                        else
                        {
                            outcome.error = "disk full";
                        }
                        outcomes.push_back(outcome);
                    }
                    return outcomes;
                });

            d.registerCommandHandler<events::terrain::CreateTerrainCommand>(
                [this](const events::terrain::CreateTerrainCommand& command)
                {
                    log.push_back("CreateTerrain");
                    createdConfig = command.config;
                    services::TerrainSummary summary = terrainSummary(createdTerrainId, "Terrain");
                    summary.data.resolution = command.config.resolution;
                    summary.data.worldTileSize = command.config.worldTileSize;
                    summary.data.minHeight = command.config.minHeight;
                    summary.data.maxHeight = command.config.maxHeight;
                    summary.data.gridMinX = -(command.config.tilesX / 2);
                    summary.data.gridMinZ = -(command.config.tilesZ / 2);
                    summary.data.gridMaxX = command.config.tilesX - command.config.tilesX / 2 - 1;
                    summary.data.gridMaxZ = command.config.tilesZ - command.config.tilesZ / 2 - 1;
                    summary.data.tileCount = static_cast<uint32_t>(command.config.tilesX * command.config.tilesZ);
                    summary.data.heightmapPath = command.config.heightmapPath;
                    terrains.push_back(summary);
                    return handleOf(createdTerrainId);
                });
            d.registerCommandHandler<events::scene::SetEntityNameCommand>(
                [this](const events::scene::SetEntityNameCommand& command)
                {
                    log.push_back("SetEntityName");
                    if (services::TerrainSummary* terrain = findTerrain(command.entity))
                    {
                        terrain->name = command.newName;
                    }
                });
            d.registerCommandHandler<events::terrain::SetTerrainMaterialPathCommand>(
                [this](const events::terrain::SetTerrainMaterialPathCommand& command)
                {
                    log.push_back("SetTerrainMaterialPath");
                    materialAssignments.push_back(command.materialPath);
                    if (services::TerrainSummary* terrain = findTerrain(command.terrainEntity))
                    {
                        terrain->data.terrainMaterialPath = command.materialPath;
                        terrain->data.saveDirty = true;
                    }
                });
            d.registerCommandHandler<events::physics::AddTerrainColliderCommand>(
                [this](const events::physics::AddTerrainColliderCommand& command)
                {
                    log.push_back("AddTerrainCollider");
                    if (services::TerrainSummary* terrain = findTerrain(command.terrainEntity))
                    {
                        terrain->hasCollider = colliderSucceeds;
                    }
                    return colliderSucceeds;
                });
            d.registerCommandHandler<events::terrain::DeleteTerrainCommand>(
                [this](const events::terrain::DeleteTerrainCommand& command)
                {
                    log.push_back("DeleteTerrain");
                    const auto before = terrains.size();
                    terrains.erase(std::remove_if(terrains.begin(), terrains.end(),
                                                  [&command](const services::TerrainSummary& terrain)
                                                  {
                                                      return terrain.entity == command.terrainEntity;
                                                  }),
                                   terrains.end());
                    return terrains.size() != before;
                });
            d.registerQueryHandler<authoring::ProbeHeightmapQuery>(
                [this](const authoring::ProbeHeightmapQuery&) { return probe; });

            d.registerQueryHandler<events::terrainMaterial::GetTerrainMaterialInfoQuery>(
                [this](const events::terrainMaterial::GetTerrainMaterialInfoQuery& query)
                    -> std::optional<services::TerrainMaterialInfo>
                {
                    auto it = materials.find(materialKey(query.materialPath));
                    if (it == materials.end())
                    {
                        return std::nullopt;
                    }
                    return it->second;
                });
            d.registerCommandHandler<events::terrainMaterial::CreateTerrainMaterialAssetCommand>(
                [this](const events::terrainMaterial::CreateTerrainMaterialAssetCommand& command)
                {
                    log.push_back("CreateTerrainMaterialAsset");
                    materialCreates.push_back(command);
                    const std::string path = (fs::path(command.directory) / (command.name + ".vfTerrainMat")).string();
                    touch(fs::path(path));
                    addMaterial(path, {command.baseLayer.name.value_or("Layer 0")});
                    materials[materialKey(path)].layers[0].materialPath = command.baseLayer.materialPath.value_or("");
                    services::CreateTerrainMaterialAssetResult result;
                    result.success = true;
                    result.path = path;
                    return result;
                });
            d.registerCommandHandler<events::terrainMaterial::EditTerrainMaterialLayerCommand>(
                [this](const events::terrainMaterial::EditTerrainMaterialLayerCommand& command)
                {
                    log.push_back("EditTerrainMaterialLayer");
                    layerEdits.push_back(command);
                    services::TerrainMaterialEditResult result;
                    auto it = materials.find(materialKey(command.materialPath));
                    if (it == materials.end())
                    {
                        result.error = "no such terrain material";
                        return result;
                    }
                    services::TerrainMaterialInfo& info = it->second;
                    uint32_t index = 0;
                    if (command.index.has_value())
                    {
                        if (*command.index >= info.activeLayerCount)
                        {
                            result.error = "no such layer";
                            return result;
                        }
                        index = *command.index;
                    }
                    else
                    {
                        index = info.activeLayerCount++;
                        info.layers.emplace_back();
                        info.layers.back().name = "Layer " + std::to_string(index);
                    }
                    services::TerrainMaterialLayerInfo& layer = info.layers[index];
                    if (command.patch.name)
                    {
                        layer.name = *command.patch.name;
                    }
                    if (command.patch.materialPath)
                    {
                        layer.materialPath = *command.patch.materialPath;
                    }
                    if (command.patch.tilingScale)
                    {
                        layer.tilingScale = *command.patch.tilingScale;
                    }
                    if (command.patch.heightContrast)
                    {
                        layer.heightContrast = *command.patch.heightContrast;
                    }
                    if (command.patch.heightBlend)
                    {
                        layer.heightBlend = *command.patch.heightBlend;
                    }
                    if (command.patch.enabled)
                    {
                        layer.enabled = *command.patch.enabled;
                    }
                    result.success = true;
                    result.index = index;
                    result.material = info;
                    return result;
                });

            d.registerCommandHandler<events::heightmapGeneration::BeginHeightmapGenerationCommand>(
                [this](const events::heightmapGeneration::BeginHeightmapGenerationCommand& command)
                {
                    log.push_back("BeginHeightmapGeneration");
                    begun = command.request;
                    services::HeightmapJobStart start;
                    start.accepted = beginAccepted;
                    start.jobId = beginAccepted ? 7 : 0;
                    if (!beginAccepted)
                    {
                        start.error = "a heightmap is already being generated";
                    }
                    return start;
                });
            d.registerCommandHandler<events::heightmapGeneration::PollHeightmapGenerationCommand>(
                [this](const events::heightmapGeneration::PollHeightmapGenerationCommand& command)
                {
                    services::HeightmapJobStatus status;
                    const std::size_t step = std::min<std::size_t>(static_cast<std::size_t>(polls), pollStates.size() - 1);
                    ++polls;
                    status.state = command.jobId == 7 ? pollStates[step] : services::HeightmapJobState::Unknown;
                    if (status.state == services::HeightmapJobState::Done && begun.has_value())
                    {
                        status.outputPath = begun->outputPath;
                        status.width = begun->resolution;
                        status.height = begun->resolution;
                        status.effective.noiseType = begun->noiseType.value_or(uint8_t{0});
                        status.effective.fractalType = begun->fractalType.value_or(uint8_t{1});
                        status.effective.octaves = begun->octaves.value_or(4);
                        status.durationMs = 12.5;
                        status.previewSize = 16;
                        status.previewRgba.assign(16 * 16 * 4, uint8_t{128});
                        touch(fs::path(status.outputPath));
                    }
                    if (status.state == services::HeightmapJobState::Failed)
                    {
                        status.error = "out of memory";
                    }
                    return status;
                });

            d.registerCommandHandler<events::undoredo::PushUndoableCommand>(
                [this](const events::undoredo::PushUndoableCommand&) { ++undoPushes; });
            d.registerCommandHandler<events::undoredo::BeginBatchCommand>(
                [this](const events::undoredo::BeginBatchCommand&) { ++batchCommands; });
            d.registerCommandHandler<events::undoredo::EndBatchCommand>(
                [this](const events::undoredo::EndBatchCommand&) { ++batchCommands; });

            d.registerCommandHandler<events::scene::SaveSceneCommand>(
                [this](const events::scene::SaveSceneCommand& command)
                {
                    log.push_back("SaveScene");
                    scenePath = command.filePath;
                    return true;
                });
            d.registerQueryHandler<events::scripting::IsScriptsCompiledQuery>(
                [](const events::scripting::IsScriptsCompiledQuery&) { return true; });
            d.registerCommandHandler<events::editor::SetEditorModeCommand>(
                [this](const events::editor::SetEditorModeCommand& command)
                {
                    log.push_back("SetEditorMode");
                    playMode = command.mode == services::EditorMode::Play;
                });
            d.registerQueryHandler<events::editor::IsEditorPausedQuery>(
                [](const events::editor::IsEditorPausedQuery&) { return false; });
            d.registerQueryHandler<events::editor::GetTimeScaleQuery>(
                [](const events::editor::GetTimeScaleQuery&) { return 1.0f; });
        }
    };

    struct TerrainToolFixture
    {
        DispatcherScope dispatcherScope;
        FakeTerrainEngine engine;
        mcp::MainThreadQueue queue;
        mcp::ToolRegistry registry;

        TerrainToolFixture()
        {
            engine.assets = makeTestDirectory("VertexForge_McpTerrainTools");
            engine.registerHandlers();
            const mcp::tools::ToolContext context{queue};
            mcp::tools::registerTerrainTools(registry, context);
            mcp::tools::registerSceneTools(registry, context);
            mcp::tools::registerPlayModeTools(registry, context);
        }

        ~TerrainToolFixture()
        {
            std::error_code ec;
            fs::remove_all(engine.assets, ec);
        }

        const mcp::ToolDef& tool(const char* name) const
        {
            auto found = registry.find(name);
            REQUIRE(found != nullptr);
            return *found;
        }

        mcp::ToolResult call(const char* name, const nlohmann::json& args)
        {
            const mcp::ToolDef& def = tool(name);
            REQUIRE(def.affinity == mcp::ThreadAffinity::Main);
            return def.handler(args);
        }

        // Worker-affinity tools block on runOnMain; play the editor main thread here.
        mcp::ToolResult callWorker(const char* name, const nlohmann::json& args)
        {
            const mcp::ToolDef& def = tool(name);
            REQUIRE(def.affinity == mcp::ThreadAffinity::Worker);
            auto future = std::async(std::launch::async, [&def, args]() { return def.handler(args); });
            while (future.wait_for(std::chrono::milliseconds(1)) != std::future_status::ready)
            {
                queue.drain(std::chrono::milliseconds(8));
            }
            return future.get();
        }

        std::string callError(const char* name, const nlohmann::json& args)
        {
            return thrownMessage([this, name, &args]() { call(name, args); });
        }

        void checkNoUndoFromMcp() const
        {
            CHECK(engine.undoPushes == 0);
            CHECK(engine.batchCommands == 0);
        }
    };

    nlohmann::json point(double x, double z)
    {
        return nlohmann::json::array({x, z});
    }

    nlohmann::json withArgs(nlohmann::json base, const nlohmann::json& extra)
    {
        for (const auto& item : extra.items())
        {
            base[item.key()] = item.value();
        }
        return base;
    }

    nlohmann::json raiseArgs(const nlohmann::json& extra = nlohmann::json::object())
    {
        return withArgs({
            {"operation", "raise"},
            {"points", nlohmann::json::array({point(0.0, 0.0)})},
            {"radius", 5.0},
            {"amount", 1.0}
        }, extra);
    }

    nlohmann::json paintArgs(const nlohmann::json& extra = nlohmann::json::object())
    {
        return withArgs({
            {"layer", 0},
            {"points", nlohmann::json::array({point(0.0, 0.0)})},
            {"radius", 5.0}
        }, extra);
    }

    void checkVec3(const nlohmann::json& value, double x, double y, double z)
    {
        REQUIRE(value.is_array());
        REQUIRE(value.size() == 3);
        CHECK(value[0].get<double>() == doctest::Approx(x));
        CHECK(value[1].get<double>() == doctest::Approx(y));
        CHECK(value[2].get<double>() == doctest::Approx(z));
    }
}

TEST_SUITE("McpTerrainTools")
{
    TEST_CASE("bad arguments are rejected before any terrain edit")
    {
        TerrainToolFixture fixture;
        fixture.engine.terrains = {terrainSummary(terrainId, "Terrain")};

        SUBCASE("ground points")
        {
            CHECK_THROWS_AS((fixture.call("terrain_sculpt", raiseArgs({{"points", "here"}}))), mcp::ArgError);
            CHECK_THROWS_AS((fixture.call("terrain_sculpt", raiseArgs({{"points", nlohmann::json::array()}}))), mcp::ArgError);
            CHECK_THROWS_AS((fixture.call("terrain_sculpt",
                raiseArgs({{"points", nlohmann::json::array({nlohmann::json::array({1.0})})}}))), mcp::ArgError);
            CHECK_THROWS_AS((fixture.call("terrain_sculpt",
                raiseArgs({{"points", nlohmann::json::array({nlohmann::json::array({1.0, "z"})})}}))), mcp::ArgError);
            CHECK_THROWS_AS((fixture.call("terrain_sculpt",
                raiseArgs({{"points", nlohmann::json::array({nlohmann::json::array({1.0, 2.0, 3.0, 4.0})})}}))),
                mcp::ArgError);

            // 1e39 is a finite double but an infinite float: it must not reach the engine.
            const std::string overflow = fixture.callError("terrain_sculpt",
                raiseArgs({{"points", nlohmann::json::array({point(1.0e39, 0.0)})}}));
            CHECK(contains(overflow, "points[0]"));
            CHECK(contains(overflow, "outside"));
            CHECK_THROWS_AS((fixture.call("terrain_sculpt",
                raiseArgs({{"points", nlohmann::json::array({point(0.0, -2.0e6)})}}))), mcp::ArgError);

            nlohmann::json tooMany = nlohmann::json::array();
            for (int i = 0; i < 257; ++i)
            {
                tooMany.push_back(point(i, 0.0));
            }
            CHECK_THROWS_AS((fixture.call("terrain_sculpt", raiseArgs({{"points", tooMany}}))), mcp::ArgError);
            CHECK_THROWS_AS((fixture.call("terrain_paint_layer", paintArgs({{"points", tooMany}}))), mcp::ArgError);
        }

        SUBCASE("ranges")
        {
            CHECK_THROWS_AS((fixture.call("terrain_sculpt", raiseArgs({{"radius", 0.0}}))), mcp::ArgError);
            CHECK_THROWS_AS((fixture.call("terrain_sculpt", raiseArgs({{"radius", 513.0}}))), mcp::ArgError);
            CHECK_THROWS_AS((fixture.call("terrain_sculpt", raiseArgs({{"amount", -1.0}}))), mcp::ArgError);
            CHECK_THROWS_AS((fixture.call("terrain_sculpt",
                raiseArgs({{"operation", "smooth"}, {"passes", 65}}))), mcp::ArgError);
            CHECK_THROWS_AS((fixture.call("terrain_sculpt",
                raiseArgs({{"operation", "flatten"}, {"strength", 1.5}}))), mcp::ArgError);
            CHECK_THROWS_AS((fixture.call("terrain_paint_layer", paintArgs({{"strength", -0.1}}))), mcp::ArgError);

            nlohmann::json noAmount = raiseArgs();
            noAmount.erase("amount");
            CHECK(contains(fixture.callError("terrain_sculpt", noAmount), "amount"));
        }

        SUBCASE("enums list the allowed values")
        {
            const std::string operation = fixture.callError("terrain_sculpt", raiseArgs({{"operation", "dig"}}));
            CHECK(contains(operation, "raise, lower, smooth, flatten"));
            CHECK(contains(operation, "'dig'"));
            CHECK(contains(fixture.callError("terrain_sculpt", raiseArgs({{"falloff", "cubic"}})),
                           "smooth, linear, sharp, constant"));
            CHECK(contains(fixture.callError("terrain_paint_layer", paintArgs({{"mode", "spray"}})), "paint, erase"));
            CHECK_THROWS_AS((fixture.call("terrain_sculpt", raiseArgs({{"shape", "hexagon"}}))), mcp::ArgError);
            CHECK_THROWS_AS((fixture.call("terrain_paint_layer", paintArgs({{"layer", true}}))), mcp::ArgError);
            CHECK_THROWS_AS((fixture.call("terrain_paint_layer", paintArgs({{"layer", -1}}))), mcp::ArgError);
        }

        SUBCASE("terrain_height_at")
        {
            CHECK_THROWS_AS((fixture.call("terrain_height_at", nlohmann::json::object())), mcp::ArgError);
            const nlohmann::json both{
                {"points", nlohmann::json::array({point(0.0, 0.0)})},
                {"area", {{"min", point(0.0, 0.0)}, {"max", point(1.0, 1.0)}}}
            };
            CHECK_THROWS_AS((fixture.call("terrain_height_at", both)), mcp::ArgError);
            const nlohmann::json inverted{{"area", {{"min", point(5.0, 0.0)}, {"max", point(1.0, 1.0)}}}};
            CHECK_THROWS_AS((fixture.call("terrain_height_at", inverted)), mcp::ArgError);
            const nlohmann::json tooFine{{"area", {{"min", point(0.0, 0.0)}, {"max", point(1.0, 1.0)}, {"samples", 33}}}};
            CHECK_THROWS_AS((fixture.call("terrain_height_at", tooFine)), mcp::ArgError);
            nlohmann::json tooMany = nlohmann::json::array();
            for (int i = 0; i < 1025; ++i)
            {
                tooMany.push_back(point(0.0, 0.0));
            }
            CHECK_THROWS_AS((fixture.call("terrain_height_at", {{"points", tooMany}})), mcp::ArgError);
            CHECK(fixture.engine.heightQueries.empty());
        }

        SUBCASE("terrain_generate_heightmap")
        {
            const std::string preset = thrownMessage([&fixture]()
            {
                fixture.callWorker("terrain_generate_heightmap", {{"preset", "volcano"}});
            });
            CHECK(contains(preset, "hills, plains, mountains, peaks, valleys, plateaus, islands, custom"));
            CHECK_THROWS_AS((fixture.callWorker("terrain_generate_heightmap", {{"resolution", 1000}})), mcp::ArgError);
            CHECK_THROWS_AS((fixture.callWorker("terrain_generate_heightmap", {{"resolution", "big"}})), mcp::ArgError);
            CHECK_THROWS_AS((fixture.callWorker("terrain_generate_heightmap", {{"seed", -1}})), mcp::ArgError);
            CHECK_THROWS_AS((fixture.callWorker("terrain_generate_heightmap", {{"featureScale", 0.0}})), mcp::ArgError);
            CHECK_THROWS_AS((fixture.callWorker("terrain_generate_heightmap", {{"overrides", {{"octaves", 40}}}})),
                            mcp::ArgError);
            CHECK(contains(thrownMessage([&fixture]()
            {
                fixture.callWorker("terrain_generate_heightmap", {{"overrides", {{"wobble", 1}}}});
            }), "wobble"));
            CHECK_FALSE(fixture.engine.begun.has_value());
        }

        CHECK(fixture.engine.log.empty());
        CHECK(fixture.engine.sculpts.empty());
        CHECK(fixture.engine.paints.empty());
        fixture.checkNoUndoFromMcp();
    }

    TEST_CASE("project paths are sandboxed and need the right extension")
    {
        TerrainToolFixture fixture;
        const fs::path& assets = fixture.engine.assets;
        touch(assets / "terrains" / "hills.png");
        touch(assets.parent_path() / "VertexForge_McpTerrainOutside.vfImage");

        CHECK_THROWS_AS((fixture.call("terrain_create", {{"heightmap", "../VertexForge_McpTerrainOutside.vfImage"}})),
                        mcp::ArgError);
        CHECK_THROWS_AS((fixture.call("terrain_create", {{"heightmap", "terrains/hills.png"}})), mcp::ArgError);
        CHECK_THROWS_AS((fixture.call("terrain_create", {{"heightmap", "terrains/missing.vfImage"}})), mcp::ArgError);
        CHECK_THROWS_AS((fixture.call("terrain_create", {{"path", "../escape.vfTerrain"}})), mcp::ArgError);
        CHECK_THROWS_AS((fixture.call("terrain_create", {{"path", "terrains/level.txt"}})), mcp::ArgError);
        CHECK_THROWS_AS((fixture.call("terrain_create", {{"terrainMaterial", "terrains/hills.png"}})), mcp::ArgError);
        CHECK(fixture.engine.log.empty());

        fixture.engine.terrains = {terrainSummary(terrainId, "Terrain")};
        CHECK_THROWS_AS((fixture.call("terrain_add_layer", {{"material", "../outside.vfMat"}})), mcp::ArgError);
        CHECK_THROWS_AS((fixture.call("terrain_add_layer", {{"material", "terrains/hills.png"}})), mcp::ArgError);
        CHECK_THROWS_AS((fixture.call("terrain_add_layer", {{"material", "materials/Missing.vfMat"}})), mcp::ArgError);
        CHECK_THROWS_AS((fixture.call("terrain_save", {{"path", "../escape.vfTerrain"}})), mcp::ArgError);
        CHECK_THROWS_AS((fixture.call("terrain_save", {{"path", "terrains/level.vfImage"}})), mcp::ArgError);
        CHECK_THROWS_AS((fixture.callWorker("terrain_generate_heightmap",
                                            {{"path", "../escape.vfImage"}, {"apply", false}})), mcp::ArgError);
        CHECK_THROWS_AS((fixture.callWorker("terrain_generate_heightmap",
                                            {{"path", "terrains/h.png"}, {"apply", false}})), mcp::ArgError);

        CHECK(fixture.engine.log.empty());
        std::error_code ec;
        fs::remove(assets.parent_path() / "VertexForge_McpTerrainOutside.vfImage", ec);
    }

    TEST_CASE("the target terrain: only live one by default, tile ids mean their terrain")
    {
        TerrainToolFixture fixture;

        SUBCASE("no terrain")
        {
            const std::string error = fixture.callError("terrain_sculpt", raiseArgs());
            CHECK(contains(error, "No terrain"));
            CHECK(contains(error, "terrain_create"));
        }

        SUBCASE("two terrains need an explicit id")
        {
            fixture.engine.terrains = {terrainSummary(terrainId, "North"), terrainSummary(otherTerrainId, "South")};
            const std::string error = fixture.callError("terrain_sculpt", raiseArgs());
            CHECK(contains(error, "pass 'terrain'"));
            CHECK(contains(error, "5 ('North')"));
            CHECK(contains(error, "9 ('South')"));

            REQUIRE_FALSE(fixture.call("terrain_sculpt", raiseArgs({{"terrain", otherTerrainId}})).isError);
            REQUIRE(fixture.engine.sculpts.size() == 1);
            CHECK(fixture.engine.sculpts[0].terrainEntity.id == otherTerrainId);
        }

        SUBCASE("an empty shell is refused with what to do")
        {
            fixture.engine.terrains = {terrainSummary(terrainId, "Broken", false)};
            const std::string error = fixture.callError("terrain_sculpt", raiseArgs());
            CHECK(contains(error, "empty shell"));
            CHECK(contains(error, "terrain_delete"));
            CHECK(contains(error, "terrain_create"));
            CHECK(fixture.engine.sculpts.empty());
        }

        SUBCASE("a tile id resolves to its terrain")
        {
            fixture.engine.terrains = {terrainSummary(terrainId, "Terrain")};
            services::EntityData tile;
            tile.handle = handleOf(tileId);
            tile.name = "Tile_0_0";
            tile.parent = handleOf(terrainId);
            tile.components = {services::ComponentTypeId::TerrainTile};
            fixture.engine.entities[tileId] = tile;

            REQUIRE_FALSE(fixture.call("terrain_sculpt", raiseArgs({{"terrain", tileId}})).isError);
            REQUIRE(fixture.engine.sculpts.size() == 1);
            CHECK(fixture.engine.sculpts[0].terrainEntity.id == terrainId);

            // Destructive: terrain_delete wants the terrain itself.
            CHECK(contains(fixture.callError("terrain_delete", {{"terrain", tileId}}), "pass the terrain id 5"));
        }

        SUBCASE("a non-terrain entity is named as such")
        {
            fixture.engine.terrains = {terrainSummary(terrainId, "Terrain")};
            services::EntityData floor;
            floor.handle = handleOf(1);
            floor.name = "Floor";
            fixture.engine.entities[1] = floor;
            const std::string error = fixture.callError("terrain_sculpt", raiseArgs({{"terrain", 1}}));
            CHECK(contains(error, "'Floor') is not a terrain"));
            CHECK(contains(error, "5 ('Terrain')"));
        }

        fixture.checkNoUndoFromMcp();
    }

    TEST_CASE("terrain_sculpt maps operations, falloff, shape and the undo label")
    {
        TerrainToolFixture fixture;
        fixture.engine.terrains = {terrainSummary(terrainId, "Terrain")};

        const nlohmann::json flatten{
            {"operation", "Flatten"},
            {"points", nlohmann::json::array({point(1.0, 2.0), nlohmann::json::array({3.0, 99.0, 5.0}),
                                              nlohmann::json{{"x", 6.0}, {"z", 7.0}}})},
            {"radius", 8.0},
            {"falloff", "constant"},
            {"shape", "square"},
            {"height", 3.0},
            {"strength", 0.5}
        };
        const mcp::ToolResult result = fixture.call("terrain_sculpt", flatten);
        REQUIRE_FALSE(result.isError);
        REQUIRE(fixture.engine.sculpts.size() == 1);
        const authoring::SculptTerrainStrokeCommand& command = fixture.engine.sculpts[0];
        CHECK(command.terrainEntity.id == terrainId);
        CHECK(command.op == authoring::SculptOp::Flatten);
        REQUIRE(command.points.size() == 3);
        CHECK(command.points[0] == glm::vec2(1.0f, 2.0f));
        CHECK(command.points[1] == glm::vec2(3.0f, 5.0f)); // y ignored
        CHECK(command.points[2] == glm::vec2(6.0f, 7.0f));
        CHECK(command.radius == doctest::Approx(8.0f));
        CHECK(command.falloff == ::terrain::BrushFalloff::Constant);
        CHECK(command.shape == ::terrain::BrushShape::Square);
        REQUIRE(command.targetHeight.has_value());
        CHECK(*command.targetHeight == doctest::Approx(3.0f));
        CHECK(command.strength == doctest::Approx(0.5f));
        CHECK(command.undoLabel == "MCP: Sculpt terrain (flatten)");

        const nlohmann::json& out = result.structured;
        CHECK(out["terrain"] == terrainId);
        CHECK(out["operation"] == "flatten");
        CHECK(out["targetHeight"].get<double>() == doctest::Approx(2.5));
        CHECK(out["tilesChanged"] == 4);
        CHECK(out["undo"]["pushed"] == true);
        CHECK(out["undo"]["label"] == "MCP: Sculpt terrain (flatten)");
        REQUIRE(out["samples"].size() == 1);
        CHECK(out["samples"][0]["before"].get<double>() == doctest::Approx(0.5));
        CHECK(out["samples"][0]["after"].get<double>() == doctest::Approx(1.5));
        CHECK(out["bounds"]["max"][1].get<double>() == doctest::Approx(4.0));

        REQUIRE_FALSE(fixture.call("terrain_sculpt", raiseArgs({{"operation", "lower"}, {"amount", 2.0}})).isError);
        CHECK(fixture.engine.sculpts[1].op == authoring::SculptOp::Lower);
        CHECK(fixture.engine.sculpts[1].amount == doctest::Approx(2.0f));
        CHECK(fixture.engine.sculpts[1].falloff == ::terrain::BrushFalloff::Smooth);
        CHECK(fixture.engine.sculpts[1].shape == ::terrain::BrushShape::Circle);
        CHECK(fixture.engine.sculpts[1].undoLabel == "MCP: Sculpt terrain (lower)");

        REQUIRE_FALSE(fixture.call("terrain_sculpt", raiseArgs({{"operation", "smooth"}, {"passes", 3}})).isError);
        CHECK(fixture.engine.sculpts[2].op == authoring::SculptOp::Smooth);
        CHECK(fixture.engine.sculpts[2].passes == 3);
        CHECK(fixture.engine.sculpts[2].strength == doctest::Approx(0.5f));

        REQUIRE_FALSE(fixture.call("terrain_sculpt", raiseArgs({{"operation", "flatten"}})).isError);
        CHECK_FALSE(fixture.engine.sculpts[3].targetHeight.has_value()); // the height under the first point
        CHECK(fixture.engine.sculpts[3].strength == doctest::Approx(1.0f));

        fixture.checkNoUndoFromMcp();
    }

    TEST_CASE("terrain_paint_layer resolves the layer by index or name")
    {
        TerrainToolFixture fixture;
        services::TerrainSummary terrain = terrainSummary(terrainId, "Terrain");

        SUBCASE("without a terrain material there is nothing to paint")
        {
            fixture.engine.terrains = {terrain};
            CHECK(contains(fixture.callError("terrain_paint_layer", paintArgs()), "terrain_add_layer"));
            CHECK(fixture.engine.paints.empty());
        }

        const std::string materialPath = (fixture.engine.assets / "terrains" / "Ground.vfTerrainMat").string();
        terrain.data.terrainMaterialPath = materialPath;
        fixture.engine.terrains = {terrain};
        fixture.engine.addMaterial(materialPath, {"Grass", "Rock"});

        SUBCASE("by name, erase")
        {
            const mcp::ToolResult result = fixture.call("terrain_paint_layer",
                paintArgs({{"layer", "rock"}, {"mode", "erase"}, {"strength", 0.25}, {"allowChannelEviction", true}}));
            REQUIRE_FALSE(result.isError);
            REQUIRE(fixture.engine.paints.size() == 1);
            const authoring::PaintTerrainLayerStrokeCommand& command = fixture.engine.paints[0];
            CHECK(command.layerIndex == 1);
            CHECK(command.op == authoring::PaintOp::Erase);
            CHECK(command.strength == doctest::Approx(0.25f));
            CHECK(command.allowChannelEviction);
            CHECK(command.undoLabel == "MCP: Erase terrain layer (Rock)");
            CHECK(result.structured["layer"]["name"] == "Rock");
            CHECK(result.structured["undo"]["label"] == "MCP: Erase terrain layer (Rock)");
        }

        SUBCASE("by index, paint")
        {
            REQUIRE_FALSE(fixture.call("terrain_paint_layer", paintArgs()).isError);
            REQUIRE(fixture.engine.paints.size() == 1);
            CHECK(fixture.engine.paints[0].layerIndex == 0);
            CHECK(fixture.engine.paints[0].op == authoring::PaintOp::Paint);
            CHECK_FALSE(fixture.engine.paints[0].allowChannelEviction);
            CHECK(fixture.engine.paints[0].undoLabel == "MCP: Paint terrain layer (Grass)");
        }

        SUBCASE("unknown layers list what exists")
        {
            const std::string byIndex = fixture.callError("terrain_paint_layer", paintArgs({{"layer", 5}}));
            CHECK(contains(byIndex, "0 'Grass', 1 'Rock'"));
            CHECK(contains(fixture.callError("terrain_paint_layer", paintArgs({{"layer", "Snow"}})), "1 'Rock'"));
            CHECK(fixture.engine.paints.empty());
        }

        fixture.checkNoUndoFromMcp();
    }

    TEST_CASE("a refused stroke becomes an agent-readable error with a hint")
    {
        TerrainToolFixture fixture;
        fixture.engine.terrains = {terrainSummary(terrainId, "Terrain")};

        struct Expectation
        {
            services::TerrainStrokeStatus status;
            const char* name;
            const char* hint;
        };
        const std::vector<Expectation> expectations{
            {services::TerrainStrokeStatus::InvalidArguments, "InvalidArguments", "arguments"},
            {services::TerrainStrokeStatus::NoTerrain, "NoTerrain", "terrain_create"},
            {services::TerrainStrokeStatus::SaveInProgress, "SaveInProgress", "retry"},
            {services::TerrainStrokeStatus::GpuUnavailable, "GpuUnavailable", "terrain_paint_layer"},
            {services::TerrainStrokeStatus::TooMuchWork, "TooMuchWork", "smaller radius or fewer points"},
            {services::TerrainStrokeStatus::OffTerrain, "OffTerrain", "X -64 to 64 and Z -64 to 64"},
            {services::TerrainStrokeStatus::NoHeightAtPoint, "NoHeightAtPoint", "'height'"}
        };
        for (const Expectation& expected : expectations)
        {
            CAPTURE(expected.name);
            fixture.engine.strokeResult.status = expected.status;
            const mcp::ToolResult result = fixture.call("terrain_sculpt", raiseArgs());
            CHECK(result.isError);
            CHECK(contains(result.text, expected.name));
            CHECK(contains(result.text, "engine refused"));
            CHECK(contains(result.text, expected.hint));
        }
        fixture.checkNoUndoFromMcp();
    }

    TEST_CASE("terrain edits are refused in Play mode and while a terrain save holds the lock")
    {
        TerrainToolFixture fixture;
        services::TerrainSummary terrain = terrainSummary(terrainId, "Terrain");
        const std::string materialPath = (fixture.engine.assets / "terrains" / "Ground.vfTerrainMat").string();
        terrain.data.terrainMaterialPath = materialPath;
        fixture.engine.terrains = {terrain};
        fixture.engine.addMaterial(materialPath, {"Grass"});
        touch(fixture.engine.assets / "materials" / "Rock.vfMat");

        auto checkRefused = [&fixture](const char* reason)
        {
            CAPTURE(reason);
            CHECK(contains(fixture.call("terrain_sculpt", raiseArgs()).text, reason));
            CHECK(contains(fixture.call("terrain_paint_layer", paintArgs()).text, reason));
            CHECK(contains(fixture.call("terrain_add_layer", {{"material", "materials/Rock.vfMat"}}).text, reason));
            CHECK(contains(fixture.call("terrain_set_layer", {{"index", 0}, {"enabled", false}}).text, reason));
            CHECK(contains(fixture.call("terrain_delete", {{"terrain", terrainId}}).text, reason));
            const mcp::ToolResult generated = fixture.callWorker("terrain_generate_heightmap", nlohmann::json::object());
            CHECK(generated.isError);
            CHECK(contains(generated.text, reason));
        };

        SUBCASE("Play mode")
        {
            fixture.engine.playMode = true;
            checkRefused("Play mode");
            CHECK(fixture.call("terrain_create", {{"name", "Second"}}).isError);
            CHECK(fixture.call("terrain_save", nlohmann::json::object()).isError);

            // Reading stays allowed.
            CHECK_FALSE(fixture.call("terrain_get_info", nlohmann::json::object()).isError);
            CHECK_FALSE(fixture.call("terrain_height_at", {{"points", nlohmann::json::array({point(0.0, 0.0)})}}).isError);
        }

        SUBCASE("save lock")
        {
            fixture.engine.saveLocked = true;
            checkRefused("terrain save is in progress");
            CHECK(fixture.call("terrain_save", nlohmann::json::object()).isError);
        }

        CHECK(fixture.engine.log.empty());
        CHECK_FALSE(fixture.engine.begun.has_value());
        fixture.checkNoUndoFromMcp();
    }

    TEST_CASE("terrain_height_at keeps input order, adds the offset and derives normals")
    {
        TerrainToolFixture fixture;
        fixture.engine.terrains = {terrainSummary(terrainId, "Terrain")};

        const nlohmann::json args{
            {"points", nlohmann::json::array({point(10.0, 0.0), point(100.0, 0.0), point(-20.0, 4.0)})},
            {"offset", 1.0},
            {"includeNormal", true}
        };
        const mcp::ToolResult result = fixture.call("terrain_height_at", args);
        REQUIRE_FALSE(result.isError);
        const nlohmann::json& out = result.structured;

        REQUIRE(out["positions"].size() == 3);
        checkVec3(out["positions"][0], 10.0, 6.0, 0.0);
        CHECK(out["positions"][1].is_null());
        checkVec3(out["positions"][2], -20.0, -9.0, 4.0);
        CHECK(out["invalid"] == nlohmann::json::array({1}));

        // h = 0.5 x: slope atan(0.5) = 26.565 degrees, normal (-0.5, 1, 0) normalised.
        REQUIRE(out["normals"].size() == 3);
        checkVec3(out["normals"][0], -0.4472136, 0.8944272, 0.0);
        CHECK(out["normals"][1].is_null());
        CHECK(out["slopeDegrees"][0].get<double>() == doctest::Approx(26.56505));
        CHECK(out["slopeDegrees"][2].get<double>() == doctest::Approx(26.56505));

        // Stats cover the terrain heights (5 and -10), not the offset.
        CHECK(out["stats"]["min"].get<double>() == doctest::Approx(-10.0));
        CHECK(out["stats"]["max"].get<double>() == doctest::Approx(5.0));
        CHECK(out["stats"]["mean"].get<double>() == doctest::Approx(-2.5));
        CHECK(out["stats"]["range"].get<double>() == doctest::Approx(15.0));
        CHECK(out["stats"]["valid"] == 2);

        // One query: the points, then four neighbours per point one vertex spacing (0.5 m) away.
        REQUIRE(fixture.engine.heightQueries.size() == 1);
        const authoring::GetTerrainHeightsQuery& query = fixture.engine.heightQueries[0];
        CHECK(query.terrainEntity.id == terrainId);
        CHECK(query.pageIn);
        REQUIRE(query.positions.size() == 15);
        CHECK(query.positions[0] == glm::vec2(10.0f, 0.0f));
        CHECK(query.positions[3] == glm::vec2(10.5f, 0.0f));
        CHECK(query.positions[4] == glm::vec2(9.5f, 0.0f));
        CHECK(query.positions[5] == glm::vec2(10.0f, 0.5f));
        CHECK(query.positions[6] == glm::vec2(10.0f, -0.5f));
    }

    TEST_CASE("terrain_height_at samples an area row by row")
    {
        TerrainToolFixture fixture;
        fixture.engine.terrains = {terrainSummary(terrainId, "Terrain")};

        const nlohmann::json args{{"area", {{"min", point(0.0, 0.0)}, {"max", point(10.0, 20.0)}, {"samples", 3}}}};
        const mcp::ToolResult result = fixture.call("terrain_height_at", args);
        REQUIRE_FALSE(result.isError);
        const nlohmann::json& positions = result.structured["positions"];
        REQUIRE(positions.size() == 9);
        checkVec3(positions[0], 0.0, 0.0, 0.0);
        checkVec3(positions[1], 5.0, 2.5, 0.0);
        checkVec3(positions[3], 0.0, 0.0, 10.0);
        checkVec3(positions[8], 10.0, 5.0, 20.0);
        CHECK(result.structured["samplesPerAxis"] == 3);
        CHECK_FALSE(result.structured.contains("normals"));
        CHECK(result.structured["stats"]["range"].get<double>() == doctest::Approx(5.0));
    }

    TEST_CASE("terrain_create dispatches create, name, material, collider, save in that order")
    {
        TerrainToolFixture fixture;
        const fs::path& assets = fixture.engine.assets;
        touch(assets / "terrains" / "ground.vfTerrainMat");

        const mcp::ToolResult result = fixture.call("terrain_create", {
            {"name", "Island"},
            {"tilesX", 2},
            {"tilesZ", 2},
            {"resolution", "low"},
            {"terrainMaterial", "terrains/ground.vfTerrainMat"}
        });
        REQUIRE_FALSE(result.isError);
        CHECK(fixture.engine.log == std::vector<std::string>{
            "CreateTerrain", "SetEntityName", "SetTerrainMaterialPath", "AddTerrainCollider", "SaveTerrains"});

        REQUIRE(fixture.engine.createdConfig.has_value());
        const services::TerrainCreationData& config = *fixture.engine.createdConfig;
        CHECK(config.tilesX == 2);
        CHECK(config.tilesZ == 2);
        CHECK(config.resolution == 0);
        CHECK(config.worldTileSize == doctest::Approx(32.0f));
        CHECK(config.minHeight == doctest::Approx(-10.0f));
        CHECK(config.maxHeight == doctest::Approx(100.0f));
        CHECK(config.heightmapPath.empty());
        CHECK(config.terrainMaterialPath.empty()); // assigned through SetTerrainMaterialPathCommand instead

        REQUIRE(fixture.engine.materialAssignments.size() == 1);
        CHECK(fs::path(fixture.engine.materialAssignments[0]) == assets / "terrains" / "ground.vfTerrainMat");
        REQUIRE(fixture.engine.saves.size() == 1);
        REQUIRE(fixture.engine.saves[0].size() == 1);
        CHECK(fixture.engine.saves[0][0].terrainEntity.id == createdTerrainId);
        CHECK(fs::path(fixture.engine.saves[0][0].path) == assets / "terrains" / "Island.vfTerrain");

        const nlohmann::json& out = result.structured;
        CHECK(out["created"] == true);
        CHECK(out["terrain"] == createdTerrainId);
        CHECK(out["name"] == "Island");
        CHECK(out["file"] == "terrains/Island.vfTerrain");
        CHECK(out["unsavedChanges"] == false);
        CHECK(out["resolution"] == "low");
        CHECK(out["verticesPerTile"] == 33);
        CHECK(out["worldBounds"]["min"] == nlohmann::json::array({-32.0, -32.0}));
        CHECK(out["worldBounds"]["max"] == nlohmann::json::array({32.0, 32.0}));
        fixture.checkNoUndoFromMcp();
    }

    TEST_CASE("terrain_create rolls back when the save fails, and refuses up front")
    {
        TerrainToolFixture fixture;
        const fs::path& assets = fixture.engine.assets;

        SUBCASE("save failure deletes the new terrain")
        {
            fixture.engine.saveSucceeds = false;
            const mcp::ToolResult result = fixture.call("terrain_create", nlohmann::json::object());
            CHECK(result.isError);
            CHECK(contains(result.text, "rolled back"));
            CHECK(contains(result.text, "disk full"));
            REQUIRE_FALSE(fixture.engine.log.empty());
            CHECK(fixture.engine.log.back() == "DeleteTerrain");
            CHECK(fixture.engine.terrains.empty());
        }

        SUBCASE("default paths are unique; collider can be skipped")
        {
            touch(assets / "terrains" / "Terrain.vfTerrain");
            REQUIRE_FALSE(fixture.call("terrain_create", {{"collider", false}}).isError);
            CHECK(std::find(fixture.engine.log.begin(), fixture.engine.log.end(), "AddTerrainCollider") ==
                  fixture.engine.log.end());
            REQUIRE(fixture.engine.saves.size() == 1);
            CHECK(fs::path(fixture.engine.saves[0][0].path) == assets / "terrains" / "Terrain_1.vfTerrain");
        }

        SUBCASE("a heightmap is probed and passed as an absolute path")
        {
            touch(assets / "terrains" / "heightmaps" / "hills_42.vfImage");
            REQUIRE_FALSE(fixture.call("terrain_create", {{"heightmap", "terrains/heightmaps/hills_42.vfImage"}}).isError);
            REQUIRE(fixture.engine.createdConfig.has_value());
            CHECK(fs::path(fixture.engine.createdConfig->heightmapPath) ==
                  assets / "terrains" / "heightmaps" / "hills_42.vfImage");
        }

        SUBCASE("a heightmap that does not load is refused before anything is created")
        {
            touch(assets / "broken.vfImage");
            fixture.engine.probe.valid = false;
            fixture.engine.probe.error = "not a .vfImage";
            const mcp::ToolResult result = fixture.call("terrain_create", {{"heightmap", "broken.vfImage"}});
            CHECK(result.isError);
            CHECK(contains(result.text, "not a .vfImage"));
            CHECK(fixture.engine.log.empty());
        }

        SUBCASE("an explicit path must not exist")
        {
            touch(assets / "terrains" / "Taken.vfTerrain");
            CHECK(fixture.call("terrain_create", {{"path", "terrains/Taken.vfTerrain"}}).isError);
            CHECK(fixture.engine.log.empty());
        }

        SUBCASE("refusals")
        {
            fixture.engine.terrains = {terrainSummary(terrainId, "Existing")};
            CHECK(contains(fixture.call("terrain_create", nlohmann::json::object()).text, "terrain_delete"));
            fixture.engine.terrains = {terrainSummary(terrainId, "Shell", false)}; // a shell does not block
            fixture.engine.creationPending = true;
            CHECK(contains(fixture.call("terrain_create", nlohmann::json::object()).text, "Create Terrain window"));
            fixture.engine.creationPending = false;
            fixture.engine.worldMode = true;
            CHECK(contains(fixture.call("terrain_create", nlohmann::json::object()).text, "World mode"));
            fixture.engine.worldMode = false;
            fixture.engine.saveLocked = true;
            CHECK(fixture.call("terrain_create", nlohmann::json::object()).isError);
            CHECK(fixture.engine.log.empty());
        }

        SUBCASE("vertex budget")
        {
            CHECK(contains(fixture.callError("terrain_create", {{"tilesX", 17}, {"tilesZ", 16}, {"resolution", "high"}}),
                           "4500000"));
            CHECK_THROWS_AS((fixture.call("terrain_create", {{"tilesX", 33}})), mcp::ArgError);
            CHECK_THROWS_AS((fixture.call("terrain_create", {{"tileSize", 4.0}})), mcp::ArgError);
            CHECK_THROWS_AS((fixture.call("terrain_create", {{"minHeight", 5.0}})), mcp::ArgError);
            CHECK_THROWS_AS((fixture.call("terrain_create", {{"maxHeight", 600.0}})), mcp::ArgError);
            CHECK(contains(fixture.callError("terrain_create", {{"resolution", "ultra"}}), "low, medium, high"));
            CHECK(fixture.engine.log.empty());

            REQUIRE_FALSE(fixture.call("terrain_create", {{"tilesX", 16}, {"tilesZ", 16}, {"resolution", "high"}}).isError);
            CHECK(fixture.engine.createdConfig->resolution == 2);
        }

        fixture.checkNoUndoFromMcp();
    }

    TEST_CASE("terrain_generate_heightmap: Begin, Poll until done, then one Apply")
    {
        TerrainToolFixture fixture;
        fixture.engine.terrains = {terrainSummary(terrainId, "Terrain")};
        fixture.engine.pollStates = {services::HeightmapJobState::Running, services::HeightmapJobState::Done};

        const mcp::ToolResult result = fixture.callWorker("terrain_generate_heightmap", {
            {"preset", "hills"},
            {"seed", 7},
            {"overrides", {{"noiseType", "simplex"}, {"fractalType", "ridged"}, {"octaves", 6}}}
        });
        REQUIRE_FALSE(result.isError);

        REQUIRE(fixture.engine.begun.has_value());
        const services::HeightmapGenerationRequest& request = *fixture.engine.begun;
        CHECK(request.preset == "hills");
        CHECK(request.seed == 7);
        // auto: 4 tiles x 64 quads + 1 = 257 samples -> 512.
        CHECK(request.resolution == 512);
        CHECK_FALSE(request.overwrite);
        CHECK(fs::path(request.outputPath) == fixture.engine.assets / "terrains" / "heightmaps" / "hills_7.vfImage");
        REQUIRE(request.noiseType.has_value());
        CHECK(*request.noiseType == 1);
        REQUIRE(request.fractalType.has_value());
        CHECK(*request.fractalType == 2);
        CHECK(request.octaves == 6);
        CHECK(fixture.engine.polls == 2);

        CHECK(fixture.engine.log == std::vector<std::string>{"BeginHeightmapGeneration", "ApplyTerrainHeightmap"});
        REQUIRE(fixture.engine.applies.size() == 1);
        const authoring::ApplyHeightmapCommand& apply = fixture.engine.applies[0];
        CHECK(apply.terrainEntity.id == terrainId);
        CHECK(apply.heightmapPath == request.outputPath);
        CHECK(apply.baseHeight == doctest::Approx(0.0f));
        CHECK(apply.amplitude == doctest::Approx(30.0f));
        CHECK(apply.undoLabel == "MCP: Apply heightmap (hills, seed 7)");

        const nlohmann::json& out = result.structured;
        CHECK(out["path"] == "terrains/heightmaps/hills_7.vfImage");
        CHECK(out["applied"] == true);
        CHECK(out["terrain"] == terrainId);
        CHECK(out["width"] == 512);
        CHECK(out["effectiveParams"]["noiseType"] == "simplex");
        CHECK(out["effectiveParams"]["fractalType"] == "ridged");
        CHECK(out["undo"]["pushed"] == true);
        CHECK(out["undo"]["label"] == "MCP: Apply heightmap (hills, seed 7)");
        REQUIRE(result.extraContent.size() == 1);
        CHECK(result.extraContent[0]["type"] == "image");
        CHECK(result.extraContent[0]["mimeType"] == "image/png");
        fixture.checkNoUndoFromMcp();
    }

    TEST_CASE("terrain_generate_heightmap: failures, apply:false and default paths")
    {
        TerrainToolFixture fixture;

        SUBCASE("applying needs a terrain")
        {
            const mcp::ToolResult result = fixture.callWorker("terrain_generate_heightmap", nlohmann::json::object());
            CHECK(result.isError);
            CHECK(contains(result.text, "terrain_create"));
            CHECK(contains(result.text, "apply:false"));
            CHECK_FALSE(fixture.engine.begun.has_value());
        }

        SUBCASE("apply:false only writes the file")
        {
            const mcp::ToolResult result = fixture.callWorker("terrain_generate_heightmap",
                                                              {{"apply", false}, {"preset", "custom"}});
            REQUIRE_FALSE(result.isError);
            REQUIRE(fixture.engine.begun.has_value());
            CHECK(fixture.engine.begun->resolution == 1024); // no terrain to size against
            CHECK(fixture.engine.begun->preset == "custom");
            CHECK(fixture.engine.applies.empty());
            CHECK(result.structured["applied"] == false);
            CHECK(result.structured["terrain"].is_null());
            CHECK(result.structured["path"] == "terrains/heightmaps/custom_42.vfImage");
        }

        SUBCASE("the job fails")
        {
            fixture.engine.terrains = {terrainSummary(terrainId, "Terrain")};
            fixture.engine.pollStates = {services::HeightmapJobState::Failed};
            const mcp::ToolResult result = fixture.callWorker("terrain_generate_heightmap", nlohmann::json::object());
            CHECK(result.isError);
            CHECK(contains(result.text, "out of memory"));
            CHECK(fixture.engine.applies.empty());
        }

        SUBCASE("the job is lost")
        {
            fixture.engine.terrains = {terrainSummary(terrainId, "Terrain")};
            fixture.engine.pollStates = {services::HeightmapJobState::Unknown};
            CHECK(contains(fixture.callWorker("terrain_generate_heightmap", nlohmann::json::object()).text, "lost"));
            CHECK(fixture.engine.applies.empty());
        }

        SUBCASE("Begin is refused")
        {
            fixture.engine.terrains = {terrainSummary(terrainId, "Terrain")};
            fixture.engine.beginAccepted = false;
            const mcp::ToolResult result = fixture.callWorker("terrain_generate_heightmap", nlohmann::json::object());
            CHECK(result.isError);
            CHECK(contains(result.text, "already being generated"));
            CHECK(fixture.engine.polls == 0);
        }

        SUBCASE("the apply is refused: the file is still reported")
        {
            fixture.engine.terrains = {terrainSummary(terrainId, "Terrain")};
            fixture.engine.strokeResult.status = services::TerrainStrokeStatus::TooMuchWork;
            const mcp::ToolResult result = fixture.callWorker("terrain_generate_heightmap", nlohmann::json::object());
            CHECK(result.isError);
            CHECK(contains(result.text, "terrains/heightmaps/hills_42.vfImage"));
            CHECK(contains(result.text, "NOT applied"));
        }

        SUBCASE("default paths are unique unless overwrite; explicit paths need overwrite")
        {
            const fs::path heightmaps = fixture.engine.assets / "terrains" / "heightmaps";
            touch(heightmaps / "hills_42.vfImage");
            REQUIRE_FALSE(fixture.callWorker("terrain_generate_heightmap", {{"apply", false}}).isError);
            CHECK(fs::path(fixture.engine.begun->outputPath) == heightmaps / "hills_42_1.vfImage");

            REQUIRE_FALSE(fixture.callWorker("terrain_generate_heightmap", {{"apply", false}, {"overwrite", true}}).isError);
            CHECK(fs::path(fixture.engine.begun->outputPath) == heightmaps / "hills_42.vfImage");
            CHECK(fixture.engine.begun->overwrite);

            const nlohmann::json explicitPath{{"apply", false}, {"path", "terrains/heightmaps/hills_42.vfImage"}};
            CHECK(contains(fixture.callWorker("terrain_generate_heightmap", explicitPath).text, "overwrite"));
        }

        fixture.checkNoUndoFromMcp();
    }

    TEST_CASE("terrain_get_info describes the selected terrain")
    {
        TerrainToolFixture fixture;

        SUBCASE("no terrain")
        {
            const mcp::ToolResult result = fixture.call("terrain_get_info", nlohmann::json::object());
            REQUIRE_FALSE(result.isError);
            CHECK(result.structured["terrain"].is_null());
            CHECK(contains(result.structured["note"].get<std::string>(), "terrain_create"));
            CHECK(result.structured["terrains"].empty());
        }

        SUBCASE("one live terrain")
        {
            services::TerrainSummary terrain = terrainSummary(terrainId, "Terrain");
            const std::string materialPath = (fixture.engine.assets / "terrains" / "Ground.vfTerrainMat").string();
            terrain.data.terrainMaterialPath = materialPath;
            terrain.hasCollider = true;
            terrain.pendingMeshTiles = 3;
            fixture.engine.terrains = {terrain, terrainSummary(otherTerrainId, "Shell", false)};
            fixture.engine.addMaterial(materialPath, {"Grass", "Rock"});

            const mcp::ToolResult result = fixture.call("terrain_get_info", nlohmann::json::object());
            REQUIRE_FALSE(result.isError);
            const nlohmann::json& out = result.structured;
            CHECK(out["terrain"] == terrainId);
            CHECK(out["tiles"]["x"] == 4);
            CHECK(out["tiles"]["count"] == 16);
            CHECK(out["resolution"] == "medium");
            CHECK(out["verticesPerTile"] == 65);
            CHECK(out["vertexSpacing"].get<double>() == doctest::Approx(0.5));
            CHECK(out["worldBounds"]["min"] == nlohmann::json::array({-64.0, -64.0}));
            CHECK(out["worldBounds"]["max"] == nlohmann::json::array({64.0, 64.0}));
            CHECK(out["material"]["path"] == "terrains/Ground.vfTerrainMat");
            REQUIRE(out["material"]["layers"].size() == 2);
            CHECK(out["material"]["layers"][1]["name"] == "Rock");
            CHECK(out["collider"].is_object());
            CHECK(out["file"].is_null());
            CHECK(out["unsavedChanges"] == true); // never saved
            CHECK(out["pendingMeshTiles"] == 3);
            CHECK(out["suggestedView"]["lookAt"] == nlohmann::json::array({0.0, 0.0, 0.0}));
            REQUIRE(out["terrains"].size() == 2);
            CHECK(out["terrains"][1]["live"] == false);

            const mcp::ToolResult shell = fixture.call("terrain_get_info", {{"terrain", otherTerrainId}});
            REQUIRE_FALSE(shell.isError);
            CHECK(shell.structured["live"] == false);
            CHECK(contains(shell.structured["note"].get<std::string>(), "terrain_delete"));
        }

        SUBCASE("several terrains")
        {
            fixture.engine.terrains = {terrainSummary(terrainId, "A"), terrainSummary(otherTerrainId, "B")};
            const mcp::ToolResult result = fixture.call("terrain_get_info", nlohmann::json::object());
            CHECK(result.structured["terrain"].is_null());
            CHECK(contains(result.structured["note"].get<std::string>(), "pass 'terrain'"));
        }
    }

    TEST_CASE("terrain_add_layer creates the palette first, then appends; terrain_set_layer patches")
    {
        TerrainToolFixture fixture;
        const fs::path& assets = fixture.engine.assets;
        touch(assets / "materials" / "Grass.vfMat");
        touch(assets / "materials" / "Rock.vfMatInstance");
        fixture.engine.terrains = {terrainSummary(terrainId, "My Terrain")};

        const mcp::ToolResult first = fixture.call("terrain_add_layer", {{"material", "materials/Grass.vfMat"}});
        REQUIRE_FALSE(first.isError);
        REQUIRE(fixture.engine.materialCreates.size() == 1);
        const auto& create = fixture.engine.materialCreates[0];
        CHECK(fs::path(create.directory) == assets / "terrains");
        CHECK(create.name == "My Terrain");
        CHECK(create.baseLayer.name == std::optional<std::string>("Grass"));
        REQUIRE(create.baseLayer.materialPath.has_value());
        CHECK(fs::path(*create.baseLayer.materialPath) == assets / "materials" / "Grass.vfMat");
        CHECK(fixture.engine.log == std::vector<std::string>{"CreateTerrainMaterialAsset", "SetTerrainMaterialPath"});
        CHECK(first.structured["created"] == true);
        CHECK(first.structured["terrainMaterial"] == "terrains/My Terrain.vfTerrainMat");
        CHECK(first.structured["layer"]["index"] == 0);
        CHECK(first.structured["layer"]["name"] == "Grass");
        CHECK(first.structured["layer"]["material"] == "materials/Grass.vfMat");
        CHECK(first.structured["layerCount"] == 1);
        CHECK(first.structured["terrainNeedsSave"] == true);

        const mcp::ToolResult second = fixture.call("terrain_add_layer",
            {{"material", "materials/Rock.vfMatInstance"}, {"name", "Cliff"}, {"tilingScale", 4.0}, {"heightBlend", true}});
        REQUIRE_FALSE(second.isError);
        REQUIRE(fixture.engine.layerEdits.size() == 1);
        CHECK_FALSE(fixture.engine.layerEdits[0].index.has_value()); // append
        CHECK(fixture.engine.layerEdits[0].patch.name == std::optional<std::string>("Cliff"));
        CHECK(fixture.engine.layerEdits[0].patch.tilingScale == std::optional<float>(4.0f));
        CHECK(fixture.engine.layerEdits[0].patch.heightBlend == std::optional<bool>(true));
        CHECK(second.structured["created"] == false);
        CHECK(second.structured["layer"]["index"] == 1);
        CHECK(second.structured["layerCount"] == 2);

        const mcp::ToolResult patched = fixture.call("terrain_set_layer", {{"index", 0}, {"enabled", false}});
        REQUIRE_FALSE(patched.isError);
        REQUIRE(fixture.engine.layerEdits.size() == 2);
        CHECK(fixture.engine.layerEdits[1].index == std::optional<uint32_t>(0));
        CHECK(fixture.engine.layerEdits[1].patch.enabled == std::optional<bool>(false));
        CHECK_FALSE(fixture.engine.layerEdits[1].patch.name.has_value());
        CHECK(patched.structured["layer"]["enabled"] == false);

        CHECK_THROWS_AS((fixture.call("terrain_set_layer", {{"index", 0}})), mcp::ArgError);
        CHECK(contains(fixture.callError("terrain_set_layer", {{"index", 7}, {"name", "X"}}), "0 'Grass', 1 'Cliff'"));
        CHECK_THROWS_AS((fixture.call("terrain_set_layer", {{"index", 0}, {"heightContrast", 17.0}})), mcp::ArgError);
        fixture.checkNoUndoFromMcp();
    }

    TEST_CASE("terrain_save: default paths, Save As and overwrite")
    {
        TerrainToolFixture fixture;
        const fs::path& assets = fixture.engine.assets;

        SUBCASE("argument-less saves every terrain that needs it, never-saved ones to unique defaults")
        {
            fixture.engine.terrains = {terrainSummary(terrainId, "Terrain"), terrainSummary(otherTerrainId, "Terrain")};
            const mcp::ToolResult result = fixture.call("terrain_save", nlohmann::json::object());
            REQUIRE_FALSE(result.isError);
            REQUIRE(fixture.engine.saves.size() == 1);
            REQUIRE(fixture.engine.saves[0].size() == 2);
            CHECK(fs::path(fixture.engine.saves[0][0].path) == assets / "terrains" / "Terrain.vfTerrain");
            CHECK(fs::path(fixture.engine.saves[0][1].path) == assets / "terrains" / "Terrain_1.vfTerrain");
            REQUIRE(result.structured["terrains"].size() == 2);
            CHECK(result.structured["terrains"][1]["path"] == "terrains/Terrain_1.vfTerrain");

            // Now both are saved: nothing left to do.
            const mcp::ToolResult again = fixture.call("terrain_save", nlohmann::json::object());
            REQUIRE_FALSE(again.isError);
            CHECK(fixture.engine.saves.size() == 1);
            CHECK(again.structured["terrains"].empty());
        }

        SUBCASE("Save As")
        {
            services::TerrainSummary terrain = terrainSummary(terrainId, "Terrain");
            terrain.data.savePath = (assets / "terrains" / "a.vfTerrain").string();
            touch(assets / "terrains" / "a.vfTerrain");
            touch(assets / "terrains" / "b.vfTerrain");
            services::TerrainSummary other = terrainSummary(otherTerrainId, "Other");
            other.data.savePath = (assets / "terrains" / "c.vfTerrain").string();
            touch(assets / "terrains" / "c.vfTerrain");
            fixture.engine.terrains = {terrain, other};

            const nlohmann::json toB{{"terrain", terrainId}, {"path", "terrains/b.vfTerrain"}};
            CHECK(contains(fixture.call("terrain_save", toB).text, "overwrite"));
            CHECK(contains(fixture.call("terrain_save", {{"terrain", terrainId}, {"path", "terrains/c.vfTerrain"},
                                                         {"overwrite", true}}).text, "Other"));
            CHECK(fixture.engine.saves.empty());

            REQUIRE_FALSE(fixture.call("terrain_save", withArgs(toB, {{"overwrite", true}})).isError);
            REQUIRE_FALSE(fixture.call("terrain_save", {{"terrain", terrainId}, {"path", "terrains/b.vfTerrain"}}).isError);
            REQUIRE(fixture.engine.saves.size() == 2);
            CHECK(fs::path(fixture.engine.saves[0][0].path) == assets / "terrains" / "b.vfTerrain");
        }

        SUBCASE("a failed save is an error")
        {
            fixture.engine.terrains = {terrainSummary(terrainId, "Terrain")};
            fixture.engine.saveSucceeds = false;
            const mcp::ToolResult result = fixture.call("terrain_save", {{"terrain", terrainId}});
            CHECK(result.isError);
            CHECK(contains(result.text, "disk full"));
        }

        fixture.checkNoUndoFromMcp();
    }

    TEST_CASE("terrain_delete removes the terrain and keeps its file")
    {
        TerrainToolFixture fixture;
        services::TerrainSummary terrain = terrainSummary(terrainId, "Terrain");
        terrain.data.savePath = (fixture.engine.assets / "terrains" / "Terrain.vfTerrain").string();
        fixture.engine.terrains = {terrain, terrainSummary(otherTerrainId, "Shell", false)};

        CHECK_THROWS_AS((fixture.call("terrain_delete", nlohmann::json::object())), mcp::ArgError);

        const mcp::ToolResult result = fixture.call("terrain_delete", {{"terrain", terrainId}});
        REQUIRE_FALSE(result.isError);
        CHECK(result.structured["terrain"] == terrainId);
        CHECK(result.structured["deleted"] == true);
        CHECK(result.structured["file"] == "terrains/Terrain.vfTerrain");

        // An empty shell can be deleted too.
        REQUIRE_FALSE(fixture.call("terrain_delete", {{"terrain", otherTerrainId}}).isError);
        CHECK(fixture.engine.terrains.empty());
        CHECK(fixture.engine.log == std::vector<std::string>{"DeleteTerrain", "DeleteTerrain"});
        fixture.checkNoUndoFromMcp();
    }

    TEST_CASE("scene_save writes terrains first and does not write the scene when one fails")
    {
        TerrainToolFixture fixture;
        fixture.engine.terrains = {terrainSummary(terrainId, "Terrain")};
        const nlohmann::json args{{"path", "scenes/Level.vfScene"}};

        SUBCASE("terrain, then scene")
        {
            const mcp::ToolResult result = fixture.call("scene_save", args);
            REQUIRE_FALSE(result.isError);
            CHECK(fixture.engine.log == std::vector<std::string>{"SaveTerrains", "SaveScene"});
            REQUIRE(result.structured["terrains"].size() == 1);
            CHECK(result.structured["terrains"][0]["path"] == "terrains/Terrain.vfTerrain");
            CHECK(fs::path(fixture.engine.scenePath) == fixture.engine.assets / "scenes" / "Level.vfScene");
        }

        SUBCASE("a failed terrain save aborts")
        {
            fixture.engine.saveSucceeds = false;
            const mcp::ToolResult result = fixture.call("scene_save", args);
            CHECK(result.isError);
            CHECK(contains(result.text, "Scene NOT saved"));
            CHECK(fixture.engine.log == std::vector<std::string>{"SaveTerrains"});
        }

        SUBCASE("saveTerrains:false skips them")
        {
            REQUIRE_FALSE(fixture.call("scene_save", withArgs(args, {{"saveTerrains", false}})).isError);
            CHECK(fixture.engine.log == std::vector<std::string>{"SaveScene"});
        }

        SUBCASE("World mode and empty shells are warnings")
        {
            fixture.engine.worldMode = true;
            const mcp::ToolResult world = fixture.call("scene_save", args);
            REQUIRE_FALSE(world.isError);
            CHECK(contains(world.structured["warnings"].dump(), "World mode"));
            CHECK(fixture.engine.log == std::vector<std::string>{"SaveScene"});

            fixture.engine.worldMode = false;
            fixture.engine.terrains = {terrainSummary(terrainId, "Broken", false)};
            const mcp::ToolResult shell = fixture.call("scene_save", args);
            REQUIRE_FALSE(shell.isError);
            CHECK(contains(shell.structured["warnings"].dump(), "empty shell"));
        }

        fixture.checkNoUndoFromMcp();
    }

    TEST_CASE("scene_save without a terrain service behaves as before")
    {
        DispatcherScope scope;
        const fs::path assets = makeTestDirectory("VertexForge_McpTerrainSceneOnly");
        mcp::MainThreadQueue queue;
        mcp::ToolRegistry registry;
        mcp::tools::registerSceneTools(registry, mcp::tools::ToolContext{queue});

        auto& d = events::EventDispatcher::instance();
        d.registerQueryHandler<events::editor::IsPlayModeQuery>([](const events::editor::IsPlayModeQuery&) { return false; });
        d.registerQueryHandler<events::project::GetCurrentProjectQuery>(
            [assets](const events::project::GetCurrentProjectQuery&) -> std::optional<config::ProjectConfig>
            {
                config::ProjectConfig config;
                config.workingDirectory = assets.string();
                return config;
            });
        int saves = 0;
        d.registerCommandHandler<events::scene::SaveSceneCommand>(
            [&saves](const events::scene::SaveSceneCommand&) { ++saves; return true; });

        const mcp::ToolResult result = registry.find("scene_save")->handler({{"path", "Level.vfScene"}});
        REQUIRE_FALSE(result.isError);
        CHECK(saves == 1);
        CHECK(result.structured["terrains"].empty());
        CHECK(result.structured["warnings"].empty());
        fs::remove_all(assets);
    }

    TEST_CASE("play_start: unsaved terrain is refused, saved or discarded")
    {
        TerrainToolFixture fixture;
        fixture.engine.terrains = {terrainSummary(terrainId, "Terrain")};

        SUBCASE("refuse by default")
        {
            const mcp::ToolResult result = fixture.call("play_start", nlohmann::json::object());
            CHECK(result.isError);
            CHECK(contains(result.text, "5 ('Terrain') (never saved)"));
            CHECK(contains(result.text, "unsavedTerrain"));
            CHECK(fixture.engine.log.empty());
            CHECK_FALSE(fixture.engine.playMode);
        }

        SUBCASE("save first")
        {
            const mcp::ToolResult result = fixture.call("play_start", {{"unsavedTerrain", "save"}});
            REQUIRE_FALSE(result.isError);
            CHECK(fixture.engine.log == std::vector<std::string>{"SaveTerrains", "SetEditorMode"});
            CHECK(result.structured["mode"] == "play");
            REQUIRE(result.structured["terrainsSaved"].size() == 1);
        }

        SUBCASE("a failed save keeps Edit mode")
        {
            fixture.engine.saveSucceeds = false;
            const mcp::ToolResult result = fixture.call("play_start", {{"unsavedTerrain", "save"}});
            CHECK(result.isError);
            CHECK(fixture.engine.log == std::vector<std::string>{"SaveTerrains"});
        }

        SUBCASE("discard")
        {
            const mcp::ToolResult result = fixture.call("play_start", {{"unsavedTerrain", "discard"}});
            REQUIRE_FALSE(result.isError);
            CHECK(fixture.engine.log == std::vector<std::string>{"SetEditorMode"});
            CHECK(contains(result.structured["warnings"].dump(), "revert"));
        }

        SUBCASE("a saved terrain needs nothing")
        {
            fixture.engine.terrains[0].data.savePath = (fixture.engine.assets / "terrains" / "Terrain.vfTerrain").string();
            touch(fixture.engine.assets / "terrains" / "Terrain.vfTerrain");
            REQUIRE_FALSE(fixture.call("play_start", nlohmann::json::object()).isError);
            CHECK(fixture.engine.log == std::vector<std::string>{"SetEditorMode"});
        }

        SUBCASE("unknown mode")
        {
            CHECK(contains(fixture.callError("play_start", {{"unsavedTerrain", "maybe"}}), "refuse, save, discard"));
        }

        fixture.checkNoUndoFromMcp();
    }

    TEST_CASE("the hierarchy reports terrain tiles as a count")
    {
        TerrainToolFixture fixture;
        events::EventDispatcher::instance().registerQueryHandler<events::scene::GetSceneHierarchyQuery>(
            [](const events::scene::GetSceneHierarchyQuery&)
            {
                auto entity = [](uint64_t id, const char* name, std::optional<uint64_t> parent,
                                 std::vector<uint64_t> children, std::vector<services::ComponentTypeId> components)
                {
                    services::EntityData data;
                    data.handle = handleOf(id);
                    data.name = name;
                    if (parent.has_value())
                    {
                        data.parent = handleOf(*parent);
                    }
                    for (uint64_t child : children)
                    {
                        data.children.push_back(handleOf(child));
                    }
                    data.components = std::move(components);
                    return data;
                };
                services::SceneHierarchyData hierarchy;
                hierarchy.root = handleOf(100);
                hierarchy.entities = {
                    entity(100, "Root", std::nullopt, {terrainId, 1}, {}),
                    entity(terrainId, "Terrain", 100, {tileId, 52, 53}, {services::ComponentTypeId::Terrain}),
                    entity(tileId, "Tile_0_0", terrainId, {}, {services::ComponentTypeId::TerrainTile}),
                    entity(52, "Tile_1_0", terrainId, {}, {services::ComponentTypeId::TerrainTile}),
                    entity(53, "Marker", terrainId, {}, {}),
                    entity(1, "Floor", 100, {}, {})
                };
                return hierarchy;
            });

        const mcp::ToolResult full = fixture.call("scene_get_hierarchy", nlohmann::json::object());
        REQUIRE_FALSE(full.isError);
        const nlohmann::json& terrainNode = full.structured["entities"][0];
        CHECK(terrainNode["id"] == terrainId);
        CHECK(terrainNode["components"] == nlohmann::json::array({"Terrain"}));
        CHECK(terrainNode["terrainTileCount"] == 2);
        REQUIRE(terrainNode["children"].size() == 1);
        CHECK(terrainNode["children"][0]["name"] == "Marker");
        CHECK(full.structured["returned"] == 3);
        CHECK(full.structured["entityCount"] == 5);

        const mcp::ToolResult shallow = fixture.call("scene_get_hierarchy", {{"maxDepth", 0}});
        const nlohmann::json& cut = shallow.structured["entities"][0];
        CHECK(cut["terrainTileCount"] == 2);
        CHECK(cut["childCount"] == 1);
        CHECK_FALSE(cut.contains("children"));
    }

    TEST_CASE("the MCP preset list feeds the generate schema")
    {
        TerrainToolFixture fixture;
        const nlohmann::json& presets = fixture.tool("terrain_generate_heightmap").inputSchema["properties"]["preset"]["enum"];
        REQUIRE(presets.size() == mcp::tools::heightmapPresetIds().size() + 1);
        for (std::size_t i = 0; i < mcp::tools::heightmapPresetIds().size(); ++i)
        {
            CHECK(presets[i] == std::string(mcp::tools::heightmapPresetIds()[i]));
        }
        CHECK(presets.back() == "custom");
        CHECK(fixture.tool("terrain_generate_heightmap").destructive);
        CHECK(fixture.tool("terrain_get_info").readOnly);
        CHECK(fixture.tool("terrain_height_at").readOnly);
        CHECK(fixture.tool("terrain_delete").destructive);
    }
}
