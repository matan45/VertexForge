#include <doctest.h>
#include <ostream> // doctest stringifies std::string_view through operator<<

#include "dispatch/MainThreadQueue.hpp"
#include "protocol/ArgReader.hpp"
#include "protocol/ToolRegistry.hpp"
#include "tools/CoreTools.hpp"
#include "tools/VFXToolSupport.hpp"
#include "undo/EntityIdRemap.hpp"

#include "asset/AssetDatabase.hpp"
#include "asset/AssetRef.hpp"
#include "vfx/VFXParamOverrideJson.hpp"
#include "vfx/VFXParameterRegistry.hpp"

#include "data/UndoTypes.hpp"
#include "events/EventDispatcher.hpp"
#include "events/asset/AssetDatabaseEvents.hpp"
#include "events/editor/EditorModeEvents.hpp"
#include "events/editor/UndoRedoEvents.hpp"
#include "events/project/ProjectEvents.hpp"
#include "events/scene/ComponentMediaEvents.hpp"
#include "events/scene/EntityTransformEvents.hpp"
#include "events/scene/SceneSettingsEvents.hpp"
#include "events/vfx/VFXAssetSummaryEvents.hpp"
#include "events/vfx/VFXEditPreviewEvents.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

// VK-1657: the MCP visual-effect tools (vfx_list_assets, vfx_add_emitter, vfx_set_params, vfx_play,
// vfx_stop), the "VFX" binding of the component_* tools and assets_rescan, against a fake engine: the VFX
// component lives in memory, the effect files exist on disk (the tools sandbox and check paths) and are
// registered in the real asset database, the authored summaries and the edit preview are canned.

namespace
{
    namespace fs = std::filesystem;
    namespace editpreview = services::events::vfxeditpreview;
    namespace vfxassets = services::events::vfxassets;

    constexpr uint64_t kRootId = 1;

    struct DispatcherScope
    {
        DispatcherScope() { clearAll(); }
        ~DispatcherScope() { clearAll(); }

        static void clearAll()
        {
            events::EventDispatcher::instance().clear();
            mcp::undo::EntityIdRemap::instance().clear();
            asset::AssetDatabase::instance().clear();
        }
    };

    fs::path makeTestDirectory(const char* name)
    {
        const fs::path directory = fs::temp_directory_path() / name;
        std::error_code ec;
        fs::remove_all(directory, ec);
        fs::create_directories(directory);
        return fs::weakly_canonical(directory);
    }

    void touch(const fs::path& path)
    {
        fs::create_directories(path.parent_path());
        std::ofstream(path, std::ios::binary | std::ios::trunc) << "{}";
    }

    // Engine paths arrive with either separator and, on Windows, any case.
    std::string pathKey(const std::string& path)
    {
        std::string key = fs::path(path).lexically_normal().generic_string();
        std::transform(key.begin(), key.end(), key.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return key;
    }

    services::EntityHandle handleOf(uint64_t id)
    {
        services::EntityHandle handle;
        handle.id = id;
        return handle;
    }

    std::size_t batchSize(const std::shared_ptr<services::IUndoableCommand>& entry)
    {
        const auto* batch = dynamic_cast<const services::BatchUndoCommand*>(entry.get());
        return batch != nullptr ? batch->getCommandCount() : 0;
    }

    bool contains(const std::string& text, const std::string& needle)
    {
        return text.find(needle) != std::string::npos;
    }

    // fire_loop's authored values (assets/vfx/library/fire_loop.vfVFX).
    services::VFXEffectSummary fireSummary()
    {
        services::VFXEffectSummary effect;
        effect.spawnRate = 40.0f;
        effect.lifetime = 0.9f;
        effect.startSize = 0.8f;
        effect.startSpeed = 1.8f;
        effect.startColor = glm::vec4(1.0f, 0.5f, 0.1f, 1.0f);
        effect.looping = true;
        effect.colorOverLifetime = true;
        effect.birthColor = glm::vec4(1.0f, 0.85f, 0.3f, 1.0f);
        effect.deathColor = glm::vec4(0.2f, 0.05f, 0.01f, 0.0f);
        effect.blendMode = vfx::VFXBlendMode::Additive;
        effect.emitsLight = true;
        return effect;
    }

    struct FakeEntity
    {
        std::string name;
        std::optional<services::VFXData> vfx;
        bool terrain = false;
    };

    struct FakeVFXEngine
    {
        fs::path assets;
        bool playMode = false;
        bool loadPending = false;
        bool previewFails = false;

        std::map<uint64_t, FakeEntity> entities;
        std::map<std::string, services::VFXAssetSummary> summaries; // pathKey -> authored summary
        std::map<uint64_t, services::VFXEditPreviewStatus> previews;
        std::vector<uint64_t> startedPreviews;
        std::vector<std::shared_ptr<services::IUndoableCommand>> undoStack;
        int rescans = 0;
        services::VFXInstanceId nextInstance = 500;

        fs::path file(const std::string& relative) const
        {
            return (assets / fs::path(relative)).lexically_normal();
        }

        // A .vfVFX on disk and in the asset database, with this authored summary.
        asset::AssetRef addEffect(const std::string& relative, services::VFXEffectSummary effect)
        {
            const fs::path path = file(relative);
            touch(path);
            const asset::AssetRef ref = asset::AssetRef::fromPath(path.string());
            REQUIRE(ref.isValid());
            services::VFXAssetSummary summary;
            summary.path = ref.resolve();
            summary.valid = true;
            summary.name = path.stem().string();
            summary.effect = std::move(effect);
            summaries[pathKey(ref.resolve())] = std::move(summary);
            return ref;
        }

        // A .vfVFXSequence combo on disk and in the asset database with one step and one cue.
        asset::AssetRef addCombo(const std::string& relative, const std::string& stepEffect)
        {
            const fs::path path = file(relative);
            touch(path);
            const asset::AssetRef ref = asset::AssetRef::fromPath(path.string());
            REQUIRE(ref.isValid());
            services::VFXAssetSummary summary;
            summary.path = ref.resolve();
            summary.isCombo = true;
            summary.valid = true;
            summary.name = path.stem().string();
            services::VFXComboSummary combo;
            services::VFXComboStepSummary step;
            step.label = "release";
            step.kind = "vfx";
            step.effectPath = file(stepEffect).string();
            step.cue = "release";
            combo.steps.push_back(step);
            combo.cues = {"release"};
            summary.combo = std::move(combo);
            summaries[pathKey(ref.resolve())] = std::move(summary);
            return ref;
        }

        void addEntity(uint64_t id, std::string name, bool terrain = false)
        {
            entities[id] = FakeEntity{std::move(name), std::nullopt, terrain};
        }

        services::VFXData& vfxOf(uint64_t id)
        {
            REQUIRE(entities.contains(id));
            REQUIRE(entities.at(id).vfx.has_value());
            return *entities.at(id).vfx;
        }

        const vfx::VFXParamOverride* overrideOf(uint64_t id, const std::string& name)
        {
            const auto& overrides = vfxOf(id).overrides;
            const auto it = std::find_if(overrides.begin(), overrides.end(),
                                         [&name](const vfx::VFXParamOverride& entry) { return entry.name == name; });
            return it != overrides.end() ? &*it : nullptr;
        }

        void registerHandlers()
        {
            auto& d = events::EventDispatcher::instance();

            d.registerQueryHandler<events::editor::IsPlayModeQuery>(
                [this](const events::editor::IsPlayModeQuery&) { return playMode; });
            d.registerQueryHandler<events::scene::IsSceneLoadPendingQuery>(
                [this](const events::scene::IsSceneLoadPendingQuery&) { return loadPending; });
            d.registerQueryHandler<events::project::GetCurrentProjectQuery>(
                [this](const events::project::GetCurrentProjectQuery&) -> std::optional<config::ProjectConfig>
                {
                    config::ProjectConfig config;
                    config.projectName = "McpVfx";
                    config.workingDirectory = assets.string();
                    return config;
                });

            // Scene.
            d.registerQueryHandler<events::scene::GetRootEntityQuery>(
                [](const events::scene::GetRootEntityQuery&) { return handleOf(kRootId); });
            d.registerQueryHandler<events::scene::GetEntityQuery>(
                [this](const events::scene::GetEntityQuery& query) -> std::optional<services::EntityData>
                {
                    const auto it = entities.find(query.entity.id);
                    if (it == entities.end())
                    {
                        return std::nullopt;
                    }
                    services::EntityData data;
                    data.handle = query.entity;
                    data.name = it->second.name;
                    if (it->second.vfx.has_value())
                    {
                        data.components.push_back(services::ComponentTypeId::VFX);
                    }
                    if (it->second.terrain)
                    {
                        data.components.push_back(services::ComponentTypeId::Terrain);
                    }
                    return data;
                });
            d.registerQueryHandler<events::scene::GetEntitiesWithComponentQuery>(
                [this](const events::scene::GetEntitiesWithComponentQuery& query)
                {
                    std::vector<services::EntityHandle> found;
                    for (const auto& [id, entity] : entities)
                    {
                        if (query.componentType == services::ComponentTypeId::VFX && entity.vfx.has_value())
                        {
                            found.push_back(handleOf(id));
                        }
                    }
                    return found;
                });

            // The VFX component, validated like VFXComponentService::setVFXData.
            d.registerCommandHandler<events::scene::AddVFXComponentCommand>(
                [this](const events::scene::AddVFXComponentCommand& command)
                {
                    const auto it = entities.find(command.entity.id);
                    if (it == entities.end() || it->second.vfx.has_value())
                    {
                        return false;
                    }
                    it->second.vfx = services::VFXData{};
                    return true;
                });
            d.registerCommandHandler<events::scene::RemoveVFXComponentCommand>(
                [this](const events::scene::RemoveVFXComponentCommand& command)
                {
                    const auto it = entities.find(command.entity.id);
                    if (it == entities.end() || !it->second.vfx.has_value())
                    {
                        return false;
                    }
                    it->second.vfx.reset();
                    return true;
                });
            d.registerQueryHandler<events::scene::GetVFXDataQuery>(
                [this](const events::scene::GetVFXDataQuery& query) -> std::optional<services::VFXData>
                {
                    const auto it = entities.find(query.entity.id);
                    return it != entities.end() ? it->second.vfx : std::nullopt;
                });
            d.registerCommandHandler<events::scene::SetVFXDataCommand>(
                [this](const events::scene::SetVFXDataCommand& command)
                {
                    const auto it = entities.find(command.entity.id);
                    if (it == entities.end())
                    {
                        return false;
                    }
                    const auto& overrides = command.vfxData.overrides;
                    for (std::size_t i = 0; i < overrides.size(); ++i)
                    {
                        if (!vfx::isValidOverride(overrides[i]))
                        {
                            return false;
                        }
                        for (std::size_t j = 0; j < i; ++j)
                        {
                            if (overrides[j].name == overrides[i].name)
                            {
                                return false;
                            }
                        }
                    }
                    it->second.vfx = command.vfxData;
                    return true;
                });

            // Asset database: the real one, filled by AssetRef::fromPath.
            d.registerQueryHandler<events::assetdb::GetAssetsByTypeQuery>(
                [](const events::assetdb::GetAssetsByTypeQuery& query)
                {
                    std::vector<events::assetdb::AssetEntryData> entries;
                    for (const asset::AssetDatabaseEntry& entry : asset::AssetDatabase::instance().getAssetsByType(query.type))
                    {
                        entries.push_back(events::assetdb::AssetEntryData{entry.guid, entry.path, entry.type});
                    }
                    return entries;
                });
            d.registerCommandHandler<events::assetdb::RegenerateMissingMetadataCommand>(
                [this](const events::assetdb::RegenerateMissingMetadataCommand&)
                {
                    ++rescans;
                    events::assetdb::RegenerateMetadataResult result;
                    result.assetsScanned = 21;
                    result.metaFilesCreated = 20;
                    return result;
                });

            // Authored summaries.
            d.registerQueryHandler<vfxassets::GetVFXAssetSummariesQuery>(
                [this](const vfxassets::GetVFXAssetSummariesQuery& query)
                {
                    std::vector<services::VFXAssetSummary> result;
                    for (const std::string& path : query.paths)
                    {
                        const auto it = summaries.find(pathKey(path));
                        if (it != summaries.end())
                        {
                            result.push_back(it->second);
                            continue;
                        }
                        services::VFXAssetSummary missing;
                        missing.path = path;
                        missing.error = "File not found: " + path;
                        result.push_back(missing);
                    }
                    return result;
                });

            // The edit preview.
            d.registerCommandHandler<editpreview::StartVFXEditPreviewCommand>(
                [this](const editpreview::StartVFXEditPreviewCommand& command)
                {
                    startedPreviews.push_back(command.entity.id);
                    services::VFXEditPreviewResult result;
                    if (previewFails)
                    {
                        result.error = "missing file or disabled at this quality tier";
                        return result;
                    }
                    const auto& vfxData = vfxOf(command.entity.id);
                    services::VFXEditPreviewStatus status;
                    status.entity = command.entity;
                    status.instanceId = nextInstance++;
                    status.assetPath = vfxData.vfxRef.resolve();
                    status.loop = vfxData.loop;
                    previews[command.entity.id] = status;
                    result.started = true;
                    result.status = status;
                    return result;
                });
            d.registerCommandHandler<editpreview::StopVFXEditPreviewCommand>(
                [this](const editpreview::StopVFXEditPreviewCommand& command)
                {
                    if (!command.entity.isValid())
                    {
                        const auto stopped = static_cast<uint32_t>(previews.size());
                        previews.clear();
                        return stopped;
                    }
                    return static_cast<uint32_t>(previews.erase(command.entity.id));
                });
            d.registerQueryHandler<editpreview::GetVFXEditPreviewsQuery>(
                [this](const editpreview::GetVFXEditPreviewsQuery& query)
                {
                    std::vector<services::VFXEditPreviewStatus> result;
                    for (const auto& [id, status] : previews)
                    {
                        if (!query.entity.isValid() || query.entity.id == id)
                        {
                            result.push_back(status);
                        }
                    }
                    return result;
                });

            d.registerCommandHandler<events::undoredo::PushUndoableCommand>(
                [this](const events::undoredo::PushUndoableCommand& command) { undoStack.push_back(command.command); });
        }
    };

    struct VFXFixture
    {
        DispatcherScope dispatcherScope;
        FakeVFXEngine engine;
        mcp::MainThreadQueue queue;
        mcp::ToolRegistry registry;

        explicit VFXFixture(const char* directory)
        {
            engine.assets = makeTestDirectory(directory);
            engine.registerHandlers();
            engine.addEntity(kRootId, "Root");
            const mcp::tools::ToolContext context{queue};
            mcp::tools::registerComponentTools(registry, context);
            mcp::tools::registerAssetTools(registry, context);
            mcp::tools::registerVFXTools(registry, context);
        }

        ~VFXFixture()
        {
            std::error_code ec;
            fs::remove_all(engine.assets, ec);
        }

        VFXFixture(const VFXFixture&) = delete;
        VFXFixture& operator=(const VFXFixture&) = delete;

        const mcp::ToolDef& tool(const char* name) const
        {
            const auto found = registry.find(name);
            REQUIRE(found != nullptr);
            return *found;
        }

        mcp::ToolResult call(const char* name, const nlohmann::json& args)
        {
            const mcp::ToolDef& def = tool(name);
            REQUIRE(def.affinity == mcp::ThreadAffinity::Main);
            return def.handler(args);
        }

        nlohmann::json callOk(const char* name, const nlohmann::json& args)
        {
            const mcp::ToolResult result = call(name, args);
            INFO(name << " failed: " << result.text);
            REQUIRE_FALSE(result.isError);
            return result.structured;
        }

        // The message of an ArgError the call throws.
        std::string argError(const char* name, const nlohmann::json& args)
        {
            try
            {
                call(name, args);
            }
            catch (const mcp::ArgError& e)
            {
                return e.what();
            }
            catch (const std::exception& e)
            {
                return std::string("<not an ArgError: ") + e.what() + ">";
            }
            return "<no error>";
        }

        // The failure of a call, whether reported in-band (isError) or thrown.
        std::string failure(const char* name, const nlohmann::json& args)
        {
            try
            {
                const mcp::ToolResult result = call(name, args);
                return result.isError ? result.text : std::string("<no error>");
            }
            catch (const std::exception& e)
            {
                return e.what();
            }
        }

        // A torch entity (10) playing vfx/fire.vfVFX through vfx_add_emitter (one undo entry).
        void addTorch()
        {
            engine.addEffect("vfx/fire.vfVFX", fireSummary());
            engine.addEntity(10, "Torch");
            callOk("vfx_add_emitter", {{"entity", 10}, {"effect", "vfx/fire.vfVFX"}});
        }
    };

    void checkJsonVec4(const nlohmann::json& value, float x, float y, float z, float w)
    {
        REQUIRE(value.is_array());
        REQUIRE(value.size() == 4);
        CHECK(value[0].get<float>() == doctest::Approx(x));
        CHECK(value[1].get<float>() == doctest::Approx(y));
        CHECK(value[2].get<float>() == doctest::Approx(z));
        CHECK(value[3].get<float>() == doctest::Approx(w));
    }
}

TEST_SUITE("McpVfxTools")
{
    TEST_CASE("registering the vfx tools dispatches nothing")
    {
        DispatcherScope scope; // no handlers at all: any dispatch would throw
        mcp::MainThreadQueue queue;
        mcp::ToolRegistry registry;
        const mcp::tools::ToolContext context{queue};
        CHECK_NOTHROW(mcp::tools::registerVFXTools(registry, context));
        CHECK(registry.size() == 5);
    }

    TEST_CASE("the vfx tools run on the main thread with closed schemas and their annotations")
    {
        VFXFixture fixture("VertexForge_McpVfx_Registration");

        struct Expected
        {
            const char* name;
            bool readOnly;
            long long timeoutMs;
        };
        for (const Expected& entry : {Expected{"vfx_list_assets", true, 30000}, Expected{"vfx_add_emitter", false, 10000},
                                      Expected{"vfx_set_params", false, 10000}, Expected{"vfx_play", false, 10000},
                                      Expected{"vfx_stop", false, 10000}, Expected{"assets_rescan", false, 120000}})
        {
            CAPTURE(entry.name);
            const mcp::ToolDef& def = fixture.tool(entry.name);
            CHECK(def.affinity == mcp::ThreadAffinity::Main);
            CHECK(def.readOnly == entry.readOnly);
            CHECK_FALSE(def.destructive);
            CHECK(def.timeout.count() == entry.timeoutMs);
            CHECK(def.inputSchema["type"] == "object");
            CHECK(def.inputSchema["additionalProperties"] == false);
        }
        CHECK(fixture.tool("vfx_add_emitter").inputSchema["required"] == nlohmann::json::array({"entity", "effect"}));

        // The params schema lists exactly the six keys, closed.
        const nlohmann::json& params = fixture.tool("vfx_set_params").inputSchema["properties"]["params"];
        CHECK(params["additionalProperties"] == false);
        CHECK(params["properties"].size() == mcp::tools::vfxtool::paramSpecs.size());
        for (const mcp::tools::vfxtool::ParamSpec& spec : mcp::tools::vfxtool::paramSpecs)
        {
            CHECK(params["properties"].contains(std::string(spec.key)));
        }
    }

    TEST_CASE("vfx_list_assets lists project effects and combos with their authored values")
    {
        VFXFixture fixture("VertexForge_McpVfx_List");

        // Nothing yet: the hint says how to bring the engine library in.
        nlohmann::json empty = fixture.callOk("vfx_list_assets", nlohmann::json::object());
        CHECK(empty["total"] == 0);
        CHECK(contains(empty["hint"].get<std::string>(), "assets_rescan"));

        fixture.engine.addEffect("vfx/fire.vfVFX", fireSummary());
        services::VFXEffectSummary smoke = fireSummary();
        smoke.colorOverLifetime = false;
        smoke.blendMode = vfx::VFXBlendMode::Alpha;
        fixture.engine.addEffect("vfx/smoke.vfVFX", smoke);
        fixture.engine.addCombo("vfx/burst.vfVFXSequence", "vfx/fire.vfVFX");
        fixture.engine.addEntity(10, "Torch");
        fixture.callOk("vfx_add_emitter", {{"entity", 10}, {"effect", "vfx/fire.vfVFX"}});

        SUBCASE("everything, sorted, with summaries and users")
        {
            nlohmann::json all = fixture.callOk("vfx_list_assets", {{"usedBy", true}});
            CHECK(all["total"] == 3);
            CHECK(all["truncated"] == false);
            REQUIRE(all["effects"].size() == 2);
            REQUIRE(all["combos"].size() == 1);

            const nlohmann::json& fire = all["effects"][0];
            CHECK(fire["path"] == "vfx/fire.vfVFX");
            CHECK(fire["valid"] == true);
            CHECK(fire["spawnRate"].get<float>() == doctest::Approx(40.0f));
            CHECK(fire["size"].get<float>() == doctest::Approx(0.8f));
            CHECK(fire["blendMode"] == "additive");
            CHECK(fire["colorOverLifetime"] == true);
            checkJsonVec4(fire["birthColor"], 1.0f, 0.85f, 0.3f, 1.0f);
            REQUIRE(fire["usedBy"].size() == 1);
            CHECK(fire["usedBy"][0]["entity"] == 10);
            CHECK(fire["usedBy"][0]["name"] == "Torch");

            CHECK(all["effects"][1]["path"] == "vfx/smoke.vfVFX");
            CHECK(all["effects"][1]["usedBy"].empty());

            const nlohmann::json& combo = all["combos"][0];
            CHECK(combo["path"] == "vfx/burst.vfVFXSequence");
            CHECK(combo["cues"] == nlohmann::json::array({"release"}));
            REQUIRE(combo["steps"].size() == 1);
            CHECK(combo["steps"][0]["effect"] == "vfx/fire.vfVFX");
        }

        SUBCASE("filters")
        {
            nlohmann::json fire = fixture.callOk("vfx_list_assets", {{"contains", "FIRE"}});
            CHECK(fire["effects"].size() == 1);
            CHECK(fire["combos"].empty());

            nlohmann::json combos = fixture.callOk("vfx_list_assets", {{"kind", "combos"}});
            CHECK(combos["effects"].empty());
            CHECK(combos["combos"].size() == 1);

            nlohmann::json first = fixture.callOk("vfx_list_assets", {{"max", 1}});
            CHECK(first["total"] == 3);
            CHECK(first["returned"] == 1);
            CHECK(first["truncated"] == true);
        }

        SUBCASE("files outside the project are not listed")
        {
            const fs::path outside = makeTestDirectory("VertexForge_McpVfx_ListOutside") / "stray.vfVFX";
            touch(outside);
            REQUIRE(asset::AssetRef::fromPath(outside.string()).isValid());
            nlohmann::json all = fixture.callOk("vfx_list_assets", nlohmann::json::object());
            CHECK(all["total"] == 3);
            std::error_code ec;
            fs::remove_all(outside.parent_path(), ec);
        }
    }

    TEST_CASE("vfx_add_emitter adds the component with its parameters as one undo step")
    {
        VFXFixture fixture("VertexForge_McpVfx_Add");
        fixture.engine.addEffect("vfx/fire.vfVFX", fireSummary());
        fixture.engine.addEntity(10, "Torch");

        nlohmann::json out = fixture.callOk("vfx_add_emitter",
            {{"entity", 10}, {"effect", "vfx/fire.vfVFX"}, {"autoPlay", false},
             {"params", {{"size", 1.6}, {"color", nlohmann::json::array({1.0, 0.6, 0.25})}}}});

        const services::VFXData& data = fixture.engine.vfxOf(10);
        CHECK(data.vfxRef.isValid());
        CHECK_FALSE(data.autoPlay);
        CHECK(data.loop);
        const vfx::VFXParamOverride* size = fixture.engine.overrideOf(10, "startSize");
        REQUIRE(size != nullptr);
        CHECK(std::get<float>(size->value) == doctest::Approx(1.6f));
        const vfx::VFXParamOverride* tint = fixture.engine.overrideOf(10, "colorTint");
        REQUIRE(tint != nullptr);
        CHECK(std::get<glm::vec4>(tint->value).w == doctest::Approx(1.0f)); // RGB input: alpha 1

        CHECK(out["data"]["vfxRef"] == "vfx/fire.vfVFX");
        CHECK(out["data"]["size"].get<float>() == doctest::Approx(1.6f));
        CHECK(out["effect"]["spawnRate"].get<float>() == doctest::Approx(40.0f));
        CHECK(out["preview"].is_null());

        REQUIRE(fixture.engine.undoStack.size() == 1);
        CHECK(batchSize(fixture.engine.undoStack.back()) == 1);

        fixture.engine.undoStack.back()->undo();
        CHECK_FALSE(fixture.engine.entities.at(10).vfx.has_value());

        fixture.engine.undoStack.back()->execute();
        REQUIRE(fixture.engine.entities.at(10).vfx.has_value());
        const vfx::VFXParamOverride* restored = fixture.engine.overrideOf(10, "startSize");
        REQUIRE(restored != nullptr);
        CHECK(std::get<float>(restored->value) == doctest::Approx(1.6f));
        CHECK_FALSE(fixture.engine.vfxOf(10).autoPlay);
    }

    TEST_CASE("vfx_add_emitter with preview starts the edit preview")
    {
        VFXFixture fixture("VertexForge_McpVfx_AddPreview");
        fixture.engine.addEffect("vfx/fire.vfVFX", fireSummary());
        fixture.engine.addEntity(10, "Torch");

        nlohmann::json out = fixture.callOk("vfx_add_emitter", {{"entity", 10}, {"effect", "vfx/fire.vfVFX"}, {"preview", true}});
        CHECK(fixture.engine.startedPreviews == std::vector<uint64_t>{10});
        CHECK(out["preview"]["playing"] == true);
        CHECK(out["preview"]["effect"] == "vfx/fire.vfVFX");
    }

    TEST_CASE("vfx_add_emitter refuses what it cannot place and changes nothing")
    {
        VFXFixture fixture("VertexForge_McpVfx_AddRefusals");
        fixture.engine.addEffect("vfx/fire.vfVFX", fireSummary());
        fixture.engine.addCombo("vfx/burst.vfVFXSequence", "vfx/fire.vfVFX");
        fixture.engine.addEntity(10, "Torch");
        fixture.engine.addEntity(20, "Ground", true);

        CHECK(contains(fixture.argError("vfx_add_emitter", {{"entity", 10}, {"effect", "vfx/burst.vfVFXSequence"}}), "combo"));
        CHECK(contains(fixture.argError("vfx_add_emitter", {{"entity", 10}, {"effect", "../elsewhere/fire.vfVFX"}}),
                       "outside"));
        CHECK(contains(fixture.argError("vfx_add_emitter", {{"entity", 10}, {"effect", "vfx/missing.vfVFX"}}),
                       "no effect"));
        CHECK(contains(fixture.argError("vfx_add_emitter",
                                        {{"entity", 10}, {"effect", "vfx/fire.vfVFX"}, {"params", {{"size", 500}}}}),
                       "size"));
        CHECK(contains(fixture.argError("vfx_add_emitter",
                                        {{"entity", 10}, {"effect", "vfx/fire.vfVFX"}, {"params", {{"glow", 1}}}}),
                       "glow"));
        CHECK(contains(fixture.failure("vfx_add_emitter", {{"entity", kRootId}, {"effect", "vfx/fire.vfVFX"}}), "root"));
        CHECK(contains(fixture.failure("vfx_add_emitter", {{"entity", 20}, {"effect", "vfx/fire.vfVFX"}}), "terrain"));
        CHECK_FALSE(fixture.engine.entities.at(10).vfx.has_value());
        CHECK(fixture.engine.undoStack.empty());

        fixture.engine.playMode = true;
        CHECK(fixture.call("vfx_add_emitter", {{"entity", 10}, {"effect", "vfx/fire.vfVFX"}}).isError);
        fixture.engine.playMode = false;
        fixture.engine.loadPending = true;
        CHECK(fixture.call("vfx_add_emitter", {{"entity", 10}, {"effect", "vfx/fire.vfVFX"}}).isError);
        fixture.engine.loadPending = false;
        CHECK_FALSE(fixture.engine.entities.at(10).vfx.has_value());

        fixture.callOk("vfx_add_emitter", {{"entity", 10}, {"effect", "vfx/fire.vfVFX"}});
        const mcp::ToolResult again = fixture.call("vfx_add_emitter", {{"entity", 10}, {"effect", "vfx/fire.vfVFX"}});
        CHECK(again.isError);
        CHECK(contains(again.text, "vfx_set_params"));
        CHECK(fixture.engine.undoStack.size() == 1);
    }

    TEST_CASE("vfx_set_params reports authored and effective values and the tinted birth colour")
    {
        VFXFixture fixture("VertexForge_McpVfx_Set");
        fixture.addTorch();
        const std::size_t undoBefore = fixture.engine.undoStack.size();

        const nlohmann::json args{{"entity", 10}, {"params", {{"size", 1.6}, {"color", nlohmann::json::array({1.0, 0.6, 0.25})}}}};
        nlohmann::json out = fixture.callOk("vfx_set_params", args);
        CHECK(out["changed"] == true);
        CHECK(out["params"]["size"]["override"].get<float>() == doctest::Approx(1.6f));
        CHECK(out["params"]["size"]["authored"].get<float>() == doctest::Approx(0.8f));
        CHECK(out["params"]["size"]["effective"].get<float>() == doctest::Approx(1.6f));
        CHECK(out["params"]["spawnRate"]["override"].is_null());
        CHECK(out["params"]["spawnRate"]["effective"].get<float>() == doctest::Approx(40.0f));
        CHECK(out["birthColor"]["source"] == "colorOverLifetime");
        checkJsonVec4(out["birthColor"]["authored"], 1.0f, 0.85f, 0.3f, 1.0f);
        checkJsonVec4(out["birthColor"]["effective"], 1.0f, 0.51f, 0.075f, 1.0f);
        REQUIRE(fixture.engine.undoStack.size() == undoBefore + 1);
        CHECK(batchSize(fixture.engine.undoStack.back()) == 1);

        // The same values again: nothing changes and no undo step is pushed.
        nlohmann::json same = fixture.callOk("vfx_set_params", args);
        CHECK(same["changed"] == false);
        CHECK(fixture.engine.undoStack.size() == undoBefore + 1);

        // Applying a tint twice reports the same effective colour (it multiplies the authored colour once).
        checkJsonVec4(same["birthColor"]["effective"], 1.0f, 0.51f, 0.075f, 1.0f);

        // Undo / redo.
        fixture.engine.undoStack.back()->undo();
        CHECK(fixture.engine.overrideOf(10, "startSize") == nullptr);
        CHECK(fixture.engine.overrideOf(10, "colorTint") == nullptr);
        fixture.engine.undoStack.back()->execute();
        REQUIRE(fixture.engine.overrideOf(10, "startSize") != nullptr);
    }

    TEST_CASE("vfx_set_params blend mode, sort order, null and reset")
    {
        VFXFixture fixture("VertexForge_McpVfx_SetReset");
        fixture.addTorch();

        fixture.callOk("vfx_set_params", {{"entity", 10}, {"params", {{"blendMode", "alpha"}, {"sortOrder", 5}, {"lifetime", 2.0}}}});
        const vfx::VFXParamOverride* blend = fixture.engine.overrideOf(10, "blendMode");
        REQUIRE(blend != nullptr);
        CHECK(std::get<int32_t>(blend->value) == static_cast<int32_t>(vfx::VFXBlendMode::Alpha));
        const vfx::VFXParamOverride* sort = fixture.engine.overrideOf(10, "sortOrder");
        REQUIRE(sort != nullptr);
        CHECK(std::get<int32_t>(sort->value) == 5);

        nlohmann::json nulled = fixture.callOk("vfx_set_params", {{"entity", 10}, {"params", {{"lifetime", nullptr}}}});
        CHECK(fixture.engine.overrideOf(10, "lifetime") == nullptr);
        CHECK(nulled["params"]["lifetime"]["effective"].get<float>() == doctest::Approx(0.9f));

        fixture.callOk("vfx_set_params", {{"entity", 10}, {"reset", nlohmann::json::array({"sortOrder"})}});
        CHECK(fixture.engine.overrideOf(10, "sortOrder") == nullptr);
        CHECK(fixture.engine.overrideOf(10, "blendMode") != nullptr);

        fixture.callOk("vfx_set_params", {{"entity", 10}, {"reset", nlohmann::json::array({"all"})}});
        CHECK(fixture.engine.vfxOf(10).overrides.empty());
    }

    TEST_CASE("vfx_set_params rejects bad arguments before writing anything")
    {
        VFXFixture fixture("VertexForge_McpVfx_SetErrors");
        fixture.addTorch();
        fixture.engine.addEntity(11, "Bare");
        const std::size_t undoBefore = fixture.engine.undoStack.size();

        CHECK(contains(fixture.argError("vfx_set_params", {{"entity", 10}}), "params"));
        CHECK(contains(fixture.argError("vfx_set_params", {{"entity", 10}, {"params", {{"size", 0.0}}}}), "size"));
        CHECK(contains(fixture.argError("vfx_set_params", {{"entity", 10}, {"params", {{"color", nlohmann::json::array({1.0, 0.5})}}}}),
                       "color"));
        CHECK(contains(fixture.argError("vfx_set_params", {{"entity", 10}, {"params", {{"color", nlohmann::json::array({-1.0, 0.5, 0.5})}}}}),
                       "color"));
        CHECK(contains(fixture.argError("vfx_set_params", {{"entity", 10}, {"params", {{"blendMode", "glow"}}}}), "blendMode"));
        CHECK(contains(fixture.argError("vfx_set_params", {{"entity", 10}, {"params", {{"sortOrder", 999}}}}), "sortOrder"));
        CHECK(contains(fixture.argError("vfx_set_params",
                                        {{"entity", 10}, {"params", {{"size", 1.0}}}, {"reset", nlohmann::json::array({"size"})}}),
                       "both"));
        CHECK(fixture.call("vfx_set_params", {{"entity", 11}, {"params", {{"size", 1.0}}}}).isError);

        fixture.engine.playMode = true;
        CHECK(fixture.call("vfx_set_params", {{"entity", 10}, {"params", {{"size", 1.0}}}}).isError);
        fixture.engine.playMode = false;

        CHECK(fixture.engine.vfxOf(10).overrides.empty());
        CHECK(fixture.engine.undoStack.size() == undoBefore);
    }

    TEST_CASE("vfx_play and vfx_stop drive the edit preview")
    {
        VFXFixture fixture("VertexForge_McpVfx_Play");
        fixture.addTorch();
        fixture.engine.addEntity(11, "Bare");

        nlohmann::json out = fixture.callOk("vfx_play", {{"entity", 10}});
        CHECK(out["playing"] == true);
        CHECK(out["effect"] == "vfx/fire.vfVFX");
        CHECK(out["lifetime"].get<float>() == doctest::Approx(0.9f));
        CHECK(contains(out["hint"].get<std::string>(), "viewport_screenshot"));
        CHECK(fixture.engine.startedPreviews == std::vector<uint64_t>{10});

        // The preview shows up in vfx_set_params replies.
        nlohmann::json set = fixture.callOk("vfx_set_params", {{"entity", 10}, {"params", {{"lifetime", 2.0}}}});
        CHECK(set["preview"]["playing"] == true);

        CHECK(fixture.call("vfx_play", {{"entity", 11}}).isError); // no VFX component

        fixture.engine.previewFails = true;
        const mcp::ToolResult failed = fixture.call("vfx_play", {{"entity", 10}});
        CHECK(failed.isError);
        CHECK(contains(failed.text, "quality tier"));
        fixture.engine.previewFails = false;

        fixture.engine.playMode = true;
        CHECK(fixture.call("vfx_play", {{"entity", 10}}).isError);
        fixture.engine.playMode = false;

        CHECK(fixture.callOk("vfx_stop", {{"entity", 10}})["stopped"] == 1);
        CHECK(fixture.callOk("vfx_stop", {{"entity", 10}})["stopped"] == 0);
        fixture.callOk("vfx_play", {{"entity", 10}});
        fixture.engine.playMode = true; // stopping works in any mode
        CHECK(fixture.callOk("vfx_stop", {{"all", true}})["stopped"] == 1);
        fixture.engine.playMode = false;

        CHECK(contains(fixture.argError("vfx_stop", nlohmann::json::object()), "exactly one"));
        CHECK(contains(fixture.argError("vfx_stop", {{"entity", 10}, {"all", true}}), "exactly one"));
    }

    TEST_CASE("component_get and component_set of type VFX use the vfx_set_params keys")
    {
        VFXFixture fixture("VertexForge_McpVfx_Binding");
        fixture.addTorch();
        fixture.engine.addCombo("vfx/burst.vfVFXSequence", "vfx/fire.vfVFX");

        nlohmann::json got = fixture.callOk("component_get", {{"entity", 10}, {"type", "VFX"}});
        CHECK(got["type"] == "VFX");
        const nlohmann::json& data = got["data"];
        for (const char* key : {"vfxRef", "autoPlay", "loop", "spawnRate", "lifetime", "size", "color", "blendMode",
                                "sortOrder", "otherParams"})
        {
            CAPTURE(key);
            CHECK(data.contains(key));
        }
        CHECK(data["vfxRef"] == "vfx/fire.vfVFX");

        fixture.callOk("component_set", {{"entity", 10}, {"type", "VFX"}, {"data", {{"otherParams", {{"startSpeed", 2.5}}}}}});
        const vfx::VFXParamOverride* speed = fixture.engine.overrideOf(10, "startSpeed");
        REQUIRE(speed != nullptr);
        CHECK(std::get<float>(speed->value) == doctest::Approx(2.5f));

        CHECK(contains(fixture.argError("component_set",
                                        {{"entity", 10}, {"type", "VFX"}, {"data", {{"vfxRef", "vfx/burst.vfVFXSequence"}}}}),
                       "combo"));
        CHECK(contains(fixture.argError("component_set", {{"entity", 10}, {"type", "VFX"}, {"data", {{"sparkle", 1}}}}),
                       "sparkle"));
    }

    TEST_CASE("undo restores a stored parameter outside the tools' range")
    {
        VFXFixture fixture("VertexForge_McpVfx_UndoStored");
        fixture.addTorch();
        // A scene file may hold what the tools refuse as input.
        fixture.engine.vfxOf(10).overrides.push_back({"spawnRate", vfx::VFXPropertyValue(std::in_place_type<float>, 5000.0f)});

        nlohmann::json out = fixture.callOk("vfx_set_params", {{"entity", 10}, {"params", {{"spawnRate", 100}}}});
        CHECK(out["changed"] == true);
        REQUIRE(fixture.engine.undoStack.size() == 2);

        fixture.engine.undoStack.back()->undo();
        const vfx::VFXParamOverride* rate = fixture.engine.overrideOf(10, "spawnRate");
        REQUIRE(rate != nullptr);
        CHECK(std::get<float>(rate->value) == doctest::Approx(5000.0f));

        fixture.engine.undoStack.back()->execute();
        rate = fixture.engine.overrideOf(10, "spawnRate");
        REQUIRE(rate != nullptr);
        CHECK(std::get<float>(rate->value) == doctest::Approx(100.0f));

        // The agent still cannot set it.
        CHECK(contains(fixture.argError("vfx_set_params", {{"entity", 10}, {"params", {{"spawnRate", 5000}}}}), "spawnRate"));
        CHECK(contains(fixture.argError("component_set", {{"entity", 10}, {"type", "VFX"}, {"data", {{"spawnRate", 5000}}}}),
                       "spawnRate"));
    }

    TEST_CASE("undoing component_remove restores an effect outside the project")
    {
        VFXFixture fixture("VertexForge_McpVfx_UndoOutside");
        // An effect outside the project, as the inspector can assign it.
        const fs::path library = makeTestDirectory("VertexForge_McpVfx_UndoOutsideLibrary");
        touch(library / "glow.vfVFX");
        touch(library / "other.vfVFX");
        const asset::AssetRef glow = asset::AssetRef::fromPath((library / "glow.vfVFX").string());
        REQUIRE(glow.isValid());
        fixture.engine.addEntity(10, "Lamp");
        services::VFXData data;
        data.vfxRef = glow;
        data.autoPlay = false;
        data.overrides.push_back({"sortOrder", vfx::VFXPropertyValue(std::in_place_type<int32_t>, 7)});
        fixture.engine.entities.at(10).vfx = data;

        // The agent cannot assign such an effect...
        CHECK(contains(fixture.argError("component_set", {{"entity", 10}, {"type", "VFX"},
                                                          {"data", {{"vfxRef", (library / "other.vfVFX").generic_string()}}}}),
                       "outside"));
        CHECK(fixture.engine.undoStack.empty());

        // ...but an undo puts it back with the rest of the component.
        fixture.callOk("component_remove", {{"entity", 10}, {"type", "VFX"}});
        CHECK_FALSE(fixture.engine.entities.at(10).vfx.has_value());
        REQUIRE(fixture.engine.undoStack.size() == 1);

        fixture.engine.undoStack.back()->undo();
        REQUIRE(fixture.engine.entities.at(10).vfx.has_value());
        CHECK(fixture.engine.vfxOf(10).vfxRef.getGUID() == glow.getGUID());
        CHECK_FALSE(fixture.engine.vfxOf(10).autoPlay);
        const vfx::VFXParamOverride* sort = fixture.engine.overrideOf(10, "sortOrder");
        REQUIRE(sort != nullptr);
        CHECK(std::get<int32_t>(sort->value) == 7);

        fixture.engine.undoStack.back()->execute();
        CHECK_FALSE(fixture.engine.entities.at(10).vfx.has_value());

        std::error_code ec;
        fs::remove_all(library, ec);
    }

    TEST_CASE("an undo that cannot restore the component leaves no component behind")
    {
        VFXFixture fixture("VertexForge_McpVfx_UndoRollback");
        fixture.addTorch();
        fixture.callOk("component_remove", {{"entity", 10}, {"type", "VFX"}});
        REQUIRE(fixture.engine.undoStack.size() == 2);

        // The effect file is gone, so the snapshot's vfxRef no longer resolves.
        std::error_code ec;
        REQUIRE(fs::remove(fixture.engine.file("vfx/fire.vfVFX"), ec));
        CHECK_NOTHROW(fixture.engine.undoStack.back()->undo()); // McpBatchUndoCommand logs the skipped step
        CHECK_FALSE(fixture.engine.entities.at(10).vfx.has_value());
    }

    TEST_CASE("assets_rescan regenerates the missing .vfmeta files and refuses in Play mode")
    {
        VFXFixture fixture("VertexForge_McpVfx_Rescan");

        nlohmann::json out = fixture.callOk("assets_rescan", nlohmann::json::object());
        CHECK(fixture.engine.rescans == 1);
        CHECK(out["scanned"] == 21);
        CHECK(out["metaFilesCreated"] == 20);

        fixture.engine.playMode = true;
        CHECK(fixture.call("assets_rescan", nlohmann::json::object()).isError);
        CHECK(fixture.engine.rescans == 1);
    }

    TEST_CASE("the parameter table round-trips its values")
    {
        using namespace mcp::tools::vfxtool;

        const std::vector<vfx::VFXParamOverride> patched = patchOverrides({},
            {{"spawnRate", 80}, {"color", nlohmann::json::array({1.0, 0.5, 0.25, 0.5})}, {"blendMode", "multiply"},
             {"sortOrder", -3}, {"otherParams", {{"gravityStrength", 2.0}}}}, "params");
        REQUIRE(patched.size() == 5);

        nlohmann::json written = nlohmann::json::object();
        writeParams(written, patched);
        CHECK(written["spawnRate"].get<float>() == doctest::Approx(80.0f));
        CHECK(written["blendMode"] == "multiply");
        CHECK(written["sortOrder"] == -3);
        CHECK(written["size"].is_null());
        CHECK(written["otherParams"]["gravityStrength"].get<float>() == doctest::Approx(2.0f));

        // A whole reported state replays unchanged (undo snapshots).
        CHECK(vfx::sameOverrides(patchOverrides(patched, written, ""), patched));
    }
}
