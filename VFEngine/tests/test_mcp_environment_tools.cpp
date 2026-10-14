#include <doctest.h>

#include "dispatch/MainThreadQueue.hpp"
#include "protocol/ArgReader.hpp"
#include "protocol/ToolRegistry.hpp"
#include "tools/CoreTools.hpp"
#include "tools/EnvironmentToolSupport.hpp"
#include "undo/EntityIdRemap.hpp"

#include "asset/AssetDatabase.hpp"
#include "asset/AssetRef.hpp"
#include "atmosphere/DayNightCycleController.hpp"
#include "atmosphere/SunEntitySync.hpp"
#include "weather/WeatherPresets.hpp"

#include "events/EventDispatcher.hpp"
#include "events/editor/EditorModeEvents.hpp"
#include "events/editor/UndoRedoEvents.hpp"
#include "events/project/ProjectEvents.hpp"
#include "events/render/AtmosphereEvents.hpp"
#include "events/render/CloudEvents.hpp"
#include "events/render/GIEvents.hpp"
#include "events/render/PostProcessEvents.hpp"
#include "events/render/RenderEvents.hpp"
#include "events/scene/ComponentMediaEvents.hpp"
#include "events/scene/ComponentPhysicsLightEvents.hpp"
#include "events/scene/EntityTransformEvents.hpp"
#include "events/scene/ReflectionProbeEvents.hpp"
#include "events/scene/ScenePersistenceEvents.hpp"
#include "events/scene/SceneSettingsEvents.hpp"
#include "events/weather/WeatherEvents.hpp"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

// VK-1654: the MCP environment & lighting tools against a fake engine that keeps both copies of every
// setting: the scene's authored types::RenderSettings (Get/SetRenderSettings) and the live renderer
// state (atmosphere, clouds, post-process, GI, IBL), plus entities with directional lights, the session
// weather and an undo stack. The log records every mutating event and RenderSettingsChanged in dispatch
// order, so the tests can pin the order of the writes.
namespace
{
    namespace fs = std::filesystem;
    namespace env = mcp::tools::env;

    using ::render::atmosphere::dayNightAngles;
    using ::render::atmosphere::directionalLightEulerForSun;
    using ::render::atmosphere::sunAnglesFromDirectionalLightEuler;

    struct DispatcherScope
    {
        DispatcherScope()
        {
            events::EventDispatcher::instance().clear();
            mcp::undo::EntityIdRemap::instance().clear();
            asset::AssetDatabase::instance().clear();
        }

        ~DispatcherScope()
        {
            events::EventDispatcher::instance().clear();
            mcp::undo::EntityIdRemap::instance().clear();
            asset::AssetDatabase::instance().clear();
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

    std::string logText(const std::vector<std::string>& log)
    {
        std::string text;
        for (const std::string& entry : log)
        {
            text += text.empty() ? entry : ", " + entry;
        }
        return "[" + text + "]";
    }

    void checkLog(const std::vector<std::string>& actual, const std::vector<std::string>& expected)
    {
        INFO("expected " << logText(expected));
        INFO("actual   " << logText(actual));
        CHECK(actual == expected);
    }

    // Position of `entry` in the log, -1 when it is not there.
    std::ptrdiff_t indexOf(const std::vector<std::string>& log, const std::string& entry)
    {
        const auto it = std::find(log.begin(), log.end(), entry);
        return it == log.end() ? -1 : std::distance(log.begin(), it);
    }

    // A schema keyword, or null when absent (const json::operator[] must never see a missing key).
    nlohmann::json schemaValue(const nlohmann::json& schema, const char* key)
    {
        return schema.is_object() && schema.contains(key) ? schema.at(key) : nlohmann::json(nullptr);
    }

    // The number of commands in a pushed undo entry (0 when it is not a batch).
    std::size_t batchSize(const std::shared_ptr<services::IUndoableCommand>& entry)
    {
        const auto* batch = dynamic_cast<const services::BatchUndoCommand*>(entry.get());
        return batch != nullptr ? batch->getCommandCount() : 0;
    }

    const char* sectionText(events::scene::RenderSettingsSection section)
    {
        switch (section)
        {
        case events::scene::RenderSettingsSection::All: return "All";
        case events::scene::RenderSettingsSection::Atmosphere: return "Atmosphere";
        case events::scene::RenderSettingsSection::Cloud: return "Cloud";
        case events::scene::RenderSettingsSection::VolumetricFog: return "VolumetricFog";
        case events::scene::RenderSettingsSection::GI: return "GI";
        case events::scene::RenderSettingsSection::IBL: return "IBL";
        }
        return "?";
    }

    // SceneGraphSystem::moveEntity's rule: insertIndex refers to the child list BEFORE the move; a
    // same-parent move from in front of the slot decrements it.
    void moveInList(std::vector<uint64_t>& oldList, std::vector<uint64_t>& newList, bool sameParent, uint64_t id,
                    int insertIndex)
    {
        if (insertIndex >= 0 && sameParent)
        {
            auto it = std::find(oldList.begin(), oldList.end(), id);
            if (it != oldList.end() && std::distance(oldList.begin(), it) < insertIndex)
            {
                --insertIndex;
            }
        }
        oldList.erase(std::remove(oldList.begin(), oldList.end(), id), oldList.end());
        if (insertIndex < 0)
        {
            newList.push_back(id);
            return;
        }
        const std::size_t index = std::min(static_cast<std::size_t>(insertIndex), newList.size());
        newList.insert(newList.begin() + static_cast<std::ptrdiff_t>(index), id);
    }

    struct FakeEnvironmentEngine
    {
        static constexpr uint64_t rootId = 100;

        struct Node
        {
            std::string name;
            bool active = true;
            std::optional<uint64_t> parent;
            std::vector<uint64_t> children;
            services::TransformData transform;
            std::optional<services::DirectionalLightData> light;
        };

        fs::path assets;
        bool playMode = false;
        bool loadPending = false;
        bool storeSucceeds = true;    // SetRenderSettingsCommand: false = no scene is open
        bool lightAddSucceeds = true; // AddDirectionalLightComponentCommand
        bool hdrLoads = true;         // SetIBLCommand
        bool probesQueued = true;     // BakeReflectionProbesCommand: false = no reflection probes

        // The authored (saved) settings and the live renderer state.
        types::RenderSettings scene;
        env::AtmosphereSettings liveAtmosphere;
        env::CloudSettings liveClouds;
        ::postprocess::PostProcessSettings livePostProcess;
        env::GISettings liveGi;
        ::render::gi::GIDebugStats giStats;

        std::optional<services::IBLData> rootIbl;   // the scene root's IBLComponent
        std::optional<std::string> rendererHdr;     // the environment the renderer has baked
        std::vector<std::string> bakedHdrPaths;     // every SetIBLCommand
        std::vector<events::render::SetIBLParamsCommand> iblParams;
        std::vector<services::EntityHandle> probeBakes;

        bool weatherEnabled = false;
        ::weather::WeatherState weatherState;
        float weatherProgress = 1.0f;
        bool scheduleEnabled = false;
        std::vector<events::weather::SetWeatherPresetCommand> presetTransitions;
        std::vector<float> weatherUpdates;

        std::map<uint64_t, Node> nodes;
        uint64_t nextId = 200;

        std::vector<std::string> log; // mutating events and RenderSettingsChanged, in dispatch order
        std::vector<events::scene::RenderSettingsChangedNotification> notifications;
        std::vector<std::shared_ptr<services::IUndoableCommand>> undoStack;
        int batchCommands = 0;

        FakeEnvironmentEngine()
        {
            nodes[rootId] = Node{"Root", true, std::nullopt, {}, {}, std::nullopt};
        }

        bool alive(uint64_t id) const { return nodes.contains(id); }

        void addEntity(uint64_t id, const std::string& name, uint64_t parent)
        {
            Node node;
            node.name = name;
            node.parent = parent;
            nodes[id] = node;
            nodes.at(parent).children.push_back(id);
        }

        uint64_t create(const std::string& name, uint64_t parent)
        {
            const uint64_t id = nextId++;
            addEntity(id, name, parent);
            return id;
        }

        // A directional light under the root, at (1, 2, 3).
        void addLight(uint64_t id, const std::string& name, bool active, const glm::vec3& rotation)
        {
            addEntity(id, name, rootId);
            Node& node = nodes.at(id);
            node.active = active;
            node.transform.position = glm::vec3(1.0f, 2.0f, 3.0f);
            node.transform.rotation = rotation;
            node.light = services::DirectionalLightData{};
        }

        std::optional<uint64_t> findByName(const std::string& name) const
        {
            for (const auto& entry : nodes)
            {
                if (entry.second.name == name)
                {
                    return entry.first;
                }
            }
            return std::nullopt;
        }

        void erase(uint64_t id)
        {
            for (uint64_t child : std::vector<uint64_t>(nodes.at(id).children))
            {
                erase(child);
            }
            nodes.erase(id);
        }

        nlohmann::json serialize(uint64_t id) const
        {
            const Node& node = nodes.at(id);
            nlohmann::json children = nlohmann::json::array();
            for (uint64_t child : node.children)
            {
                children.push_back(serialize(child));
            }
            nlohmann::json out{
                {"name", node.name},
                {"isActive", node.active},
                {"transform", {
                    {"position", mcp::vec3ToJson(node.transform.position)},
                    {"rotation", mcp::vec3ToJson(node.transform.rotation)},
                    {"scale", mcp::vec3ToJson(node.transform.scale)}
                }},
                {"children", std::move(children)}
            };
            if (node.light.has_value())
            {
                out["directionalLight"] = {
                    {"color", mcp::vec3ToJson(node.light->color)},
                    {"intensity", node.light->intensity},
                    {"lightSize", node.light->lightSize},
                    {"showGizmo", node.light->showGizmo}
                };
            }
            return out;
        }

        uint64_t instantiate(const nlohmann::json& json, uint64_t parent)
        {
            const uint64_t id = create(json.value("name", std::string("Entity")), parent);
            Node& node = nodes.at(id);
            node.active = json.value("isActive", true);
            const nlohmann::json& transform = json.at("transform");
            node.transform.position = mcp::ArgReader::toVec3(transform.at("position"), "position");
            node.transform.rotation = mcp::ArgReader::toVec3(transform.at("rotation"), "rotation");
            node.transform.scale = mcp::ArgReader::toVec3(transform.at("scale"), "scale");
            if (json.contains("directionalLight"))
            {
                const nlohmann::json& light = json.at("directionalLight");
                services::DirectionalLightData data;
                data.color = mcp::ArgReader::toVec3(light.at("color"), "color");
                data.intensity = light.at("intensity").get<float>();
                data.lightSize = light.at("lightSize").get<float>();
                data.showGizmo = light.at("showGizmo").get<bool>();
                node.light = data;
            }
            for (const nlohmann::json& child : json.at("children"))
            {
                instantiate(child, id);
            }
            return id;
        }

        // WeatherServiceImpl::applyFogWeather: while enabled, weather owns the live fog density and colour.
        void applyFogWeather()
        {
            ::postprocess::VolumetricFogSettings& fog = livePostProcess.volumetricFog;
            fog.uniformDensity = weatherState.fogDensity;
            fog.heightFogDensity = weatherState.heightFogDensity;
            fog.fogColor[0] = weatherState.atmosphereTint.x;
            fog.fogColor[1] = weatherState.atmosphereTint.y;
            fog.fogColor[2] = weatherState.atmosphereTint.z;
        }

        // WeatherServiceImpl::cleanupEffects: switching weather off gives the scene its own fog back.
        void restoreAuthoredFog()
        {
            const ::postprocess::VolumetricFogSettings& authored = scene.postProcess.volumetricFog;
            ::postprocess::VolumetricFogSettings& fog = livePostProcess.volumetricFog;
            fog.uniformDensity = authored.uniformDensity;
            fog.heightFogDensity = authored.heightFogDensity;
            fog.fogColor[0] = authored.fogColor[0];
            fog.fogColor[1] = authored.fogColor[1];
            fog.fogColor[2] = authored.fogColor[2];
        }

        void registerHandlers()
        {
            auto& d = events::EventDispatcher::instance();

            // Editor and project state.
            d.registerQueryHandler<events::editor::IsPlayModeQuery>(
                [this](const events::editor::IsPlayModeQuery&) { return playMode; });
            d.registerQueryHandler<events::scene::IsSceneLoadPendingQuery>(
                [this](const events::scene::IsSceneLoadPendingQuery&) { return loadPending; });
            d.registerQueryHandler<events::project::GetCurrentProjectQuery>(
                [this](const events::project::GetCurrentProjectQuery&) -> std::optional<config::ProjectConfig>
                {
                    config::ProjectConfig config;
                    config.projectName = "McpEnvironment";
                    config.workingDirectory = assets.string();
                    return config;
                });

            // The authored copy.
            d.registerQueryHandler<events::scene::GetRenderSettingsQuery>(
                [this](const events::scene::GetRenderSettingsQuery&) { return scene; });
            d.registerCommandHandler<events::scene::SetRenderSettingsCommand>(
                [this](const events::scene::SetRenderSettingsCommand& command)
                {
                    log.push_back("SetRenderSettings");
                    if (storeSucceeds)
                    {
                        scene = command.settings;
                    }
                    return storeSucceeds;
                });

            // The live copies.
            d.registerQueryHandler<events::atmosphere::GetAtmosphereSettingsQuery>(
                [this](const events::atmosphere::GetAtmosphereSettingsQuery&) { return liveAtmosphere; });
            d.registerCommandHandler<events::atmosphere::ApplyAtmosphereSettingsNoSunCommand>(
                [this](const events::atmosphere::ApplyAtmosphereSettingsNoSunCommand& command)
                {
                    log.push_back("ApplyAtmosphereSettingsNoSun");
                    liveAtmosphere = command.settings;
                });
            // The auto-Sun apply: the tools must never use it.
            d.registerCommandHandler<events::atmosphere::ApplyAtmosphereSettingsCommand>(
                [this](const events::atmosphere::ApplyAtmosphereSettingsCommand& command)
                {
                    log.push_back("ApplyAtmosphereSettings");
                    liveAtmosphere = command.settings;
                });
            d.registerQueryHandler<events::cloud::GetCloudSettingsQuery>(
                [this](const events::cloud::GetCloudSettingsQuery&) { return liveClouds; });
            d.registerCommandHandler<events::cloud::ApplyCloudSettingsCommand>(
                [this](const events::cloud::ApplyCloudSettingsCommand& command)
                {
                    log.push_back("ApplyCloudSettings");
                    liveClouds = command.settings;
                });
            d.registerQueryHandler<events::postprocess::GetPostProcessSettingsQuery>(
                [this](const events::postprocess::GetPostProcessSettingsQuery&) { return livePostProcess; });
            d.registerCommandHandler<events::postprocess::ApplyPostProcessSettingsCommand>(
                [this](const events::postprocess::ApplyPostProcessSettingsCommand& command)
                {
                    log.push_back("ApplyPostProcessSettings");
                    livePostProcess = command.settings;
                });

            // GI (GIAdapter: the debug toggles are read-modify-write of the live settings).
            d.registerQueryHandler<events::render::gi::GetGISettingsQuery>(
                [this](const events::render::gi::GetGISettingsQuery&) { return liveGi; });
            d.registerCommandHandler<events::render::gi::ApplyGISettingsCommand>(
                [this](const events::render::gi::ApplyGISettingsCommand& command)
                {
                    log.push_back("ApplyGISettings");
                    liveGi = command.settings;
                });
            d.registerQueryHandler<events::render::gi::GetGIDebugStatsQuery>(
                [this](const events::render::gi::GetGIDebugStatsQuery&) { return giStats; });
            d.registerQueryHandler<events::render::gi::IsGIEnabledQuery>(
                [this](const events::render::gi::IsGIEnabledQuery&) { return liveGi.enabled; });
            d.registerCommandHandler<events::render::gi::SetGIDebugProbesCommand>(
                [this](const events::render::gi::SetGIDebugProbesCommand& command)
                {
                    log.push_back("SetGIDebugProbes");
                    liveGi.showProbes = command.show;
                });
            d.registerCommandHandler<events::render::gi::SetGIDebugCascadeBoundsCommand>(
                [this](const events::render::gi::SetGIDebugCascadeBoundsCommand& command)
                {
                    log.push_back("SetGIDebugCascadeBounds");
                    liveGi.showCascadeBounds = command.show;
                });
            d.registerCommandHandler<events::render::gi::SetGIDebugProbeValidityCommand>(
                [this](const events::render::gi::SetGIDebugProbeValidityCommand& command)
                {
                    log.push_back("SetGIDebugProbeValidity");
                    liveGi.showProbeValidity = command.show;
                });

            // Image-based lighting: the root's component and the renderer's baked environment.
            d.registerQueryHandler<events::scene::GetRootEntityQuery>(
                [](const events::scene::GetRootEntityQuery&) { return handleOf(rootId); });
            d.registerQueryHandler<events::scene::GetIBLDataQuery>(
                [this](const events::scene::GetIBLDataQuery& query) -> std::optional<services::IBLData>
                {
                    if (query.entity.id != rootId)
                    {
                        return std::nullopt;
                    }
                    return rootIbl;
                });
            d.registerCommandHandler<events::scene::SetIBLDataCommand>(
                [this](const events::scene::SetIBLDataCommand& command)
                {
                    log.push_back("SetIBLData");
                    if (command.entity.id != rootId)
                    {
                        return false;
                    }
                    rootIbl = command.iblData;
                    return true;
                });
            d.registerCommandHandler<events::scene::RemoveIBLComponentCommand>(
                [this](const events::scene::RemoveIBLComponentCommand& command)
                {
                    log.push_back("RemoveIBLComponent");
                    const bool had = command.entity.id == rootId && rootIbl.has_value();
                    if (had)
                    {
                        rootIbl.reset();
                    }
                    return had;
                });
            d.registerCommandHandler<events::render::SetIBLCommand>(
                [this](const events::render::SetIBLCommand& command)
                {
                    log.push_back("SetIBL");
                    bakedHdrPaths.push_back(command.hdrPath);
                    if (hdrLoads)
                    {
                        rendererHdr = command.hdrPath;
                    }
                    return hdrLoads;
                });
            d.registerCommandHandler<events::render::SetIBLParamsCommand>(
                [this](const events::render::SetIBLParamsCommand& command)
                {
                    log.push_back("SetIBLParams");
                    iblParams.push_back(command);
                });
            d.registerCommandHandler<events::render::RemoveIBLCommand>(
                [this](const events::render::RemoveIBLCommand&)
                {
                    log.push_back("RemoveIBL");
                    rendererHdr.reset();
                });
            d.registerCommandHandler<events::scene::BakeReflectionProbesCommand>(
                [this](const events::scene::BakeReflectionProbesCommand& command)
                {
                    log.push_back("BakeReflectionProbes");
                    probeBakes.push_back(command.entity);
                    return probesQueued;
                });

            // Entities and directional lights.
            d.registerQueryHandler<events::atmosphere::GetSunEntityQuery>(
                [this](const events::atmosphere::GetSunEntityQuery& query) -> std::optional<services::EntityHandle>
                {
                    for (const auto& entry : nodes)
                    {
                        if (entry.second.light.has_value() && (!query.activeOnly || entry.second.active))
                        {
                            return handleOf(entry.first);
                        }
                    }
                    return std::nullopt;
                });
            d.registerQueryHandler<events::scene::GetEntityQuery>(
                [this](const events::scene::GetEntityQuery& query) -> std::optional<services::EntityData>
                {
                    if (!alive(query.entity.id))
                    {
                        return std::nullopt;
                    }
                    const Node& node = nodes.at(query.entity.id);
                    services::EntityData data;
                    data.handle = query.entity;
                    data.name = node.name;
                    data.isActive = node.active;
                    data.isEffectivelyActive = node.active;
                    if (node.parent.has_value())
                    {
                        data.parent = handleOf(*node.parent);
                    }
                    for (uint64_t child : node.children)
                    {
                        data.children.push_back(handleOf(child));
                    }
                    data.localTransform = node.transform;
                    data.worldTransform = node.transform;
                    if (node.light.has_value())
                    {
                        data.components.push_back(services::ComponentTypeId::DirectionalLight);
                    }
                    return data;
                });
            d.registerQueryHandler<events::scene::GetTransformQuery>(
                [this](const events::scene::GetTransformQuery& query) -> std::optional<services::TransformData>
                {
                    if (!alive(query.entity.id))
                    {
                        return std::nullopt;
                    }
                    return nodes.at(query.entity.id).transform;
                });
            d.registerCommandHandler<events::scene::SetTransformCommand>(
                [this](const events::scene::SetTransformCommand& command)
                {
                    log.push_back("SetTransform");
                    if (alive(command.entity.id))
                    {
                        nodes.at(command.entity.id).transform = command.transform;
                    }
                });
            d.registerCommandHandler<events::scene::CreateEntityCommand>(
                [this](const events::scene::CreateEntityCommand& command)
                {
                    log.push_back("CreateEntity");
                    const uint64_t parent =
                        command.parent.has_value() && alive(command.parent->id) ? command.parent->id : rootId;
                    return handleOf(create(command.name, parent));
                });
            d.registerCommandHandler<events::scene::DeleteEntityCommand>(
                [this](const events::scene::DeleteEntityCommand& command)
                {
                    log.push_back("DeleteEntity");
                    if (!alive(command.entity.id) || command.entity.id == rootId)
                    {
                        return false;
                    }
                    std::vector<uint64_t>& siblings = nodes.at(*nodes.at(command.entity.id).parent).children;
                    siblings.erase(std::remove(siblings.begin(), siblings.end(), command.entity.id), siblings.end());
                    erase(command.entity.id);
                    return true;
                });
            d.registerQueryHandler<events::scene::CopyEntityToJsonQuery>(
                [this](const events::scene::CopyEntityToJsonQuery& query) -> std::string
                {
                    if (!alive(query.entity.id) || query.entity.id == rootId)
                    {
                        return {};
                    }
                    return serialize(query.entity.id).dump();
                });
            d.registerCommandHandler<events::scene::InstantiateEntityFromJsonCommand>(
                [this](const events::scene::InstantiateEntityFromJsonCommand& command)
                    -> std::optional<services::EntityHandle>
                {
                    log.push_back("InstantiateEntityFromJson");
                    const uint64_t parent =
                        command.parent.has_value() && alive(command.parent->id) ? command.parent->id : rootId;
                    return handleOf(instantiate(nlohmann::json::parse(command.jsonText), parent));
                });
            d.registerCommandHandler<events::scene::ReorderEntityCommand>(
                [this](const events::scene::ReorderEntityCommand& command)
                {
                    log.push_back("ReorderEntity");
                    if (!alive(command.entity.id) || !alive(command.newParent.id))
                    {
                        return false;
                    }
                    Node& node = nodes.at(command.entity.id);
                    const uint64_t oldParent = *node.parent;
                    moveInList(nodes.at(oldParent).children, nodes.at(command.newParent.id).children,
                               oldParent == command.newParent.id, command.entity.id, command.insertIndex);
                    node.parent = command.newParent.id;
                    return true;
                });
            d.registerCommandHandler<events::scene::AddDirectionalLightComponentCommand>(
                [this](const events::scene::AddDirectionalLightComponentCommand& command)
                {
                    log.push_back("AddDirectionalLight");
                    if (!lightAddSucceeds || !alive(command.entity.id) || nodes.at(command.entity.id).light.has_value())
                    {
                        return false;
                    }
                    nodes.at(command.entity.id).light = services::DirectionalLightData{};
                    return true;
                });
            d.registerCommandHandler<events::scene::RemoveDirectionalLightComponentCommand>(
                [this](const events::scene::RemoveDirectionalLightComponentCommand& command)
                {
                    log.push_back("RemoveDirectionalLight");
                    if (!alive(command.entity.id) || !nodes.at(command.entity.id).light.has_value())
                    {
                        return false;
                    }
                    nodes.at(command.entity.id).light.reset();
                    return true;
                });
            d.registerQueryHandler<events::scene::GetDirectionalLightDataQuery>(
                [this](const events::scene::GetDirectionalLightDataQuery& query)
                    -> std::optional<services::DirectionalLightData>
                {
                    if (!alive(query.entity.id))
                    {
                        return std::nullopt;
                    }
                    return nodes.at(query.entity.id).light;
                });
            d.registerCommandHandler<events::scene::SetDirectionalLightDataCommand>(
                [this](const events::scene::SetDirectionalLightDataCommand& command)
                {
                    log.push_back("SetDirectionalLightData");
                    if (!alive(command.entity.id) || !nodes.at(command.entity.id).light.has_value())
                    {
                        return false;
                    }
                    nodes.at(command.entity.id).light = command.lightData;
                    return true;
                });

            // The session weather.
            d.registerQueryHandler<events::weather::IsWeatherEnabledQuery>(
                [this](const events::weather::IsWeatherEnabledQuery&) { return weatherEnabled; });
            d.registerQueryHandler<events::weather::GetWeatherStateQuery>(
                [this](const events::weather::GetWeatherStateQuery&) { return weatherState; });
            d.registerQueryHandler<events::weather::GetWeatherTransitionProgressQuery>(
                [this](const events::weather::GetWeatherTransitionProgressQuery&) { return weatherProgress; });
            d.registerQueryHandler<events::weather::IsWeatherScheduleEnabledQuery>(
                [this](const events::weather::IsWeatherScheduleEnabledQuery&) { return scheduleEnabled; });
            d.registerCommandHandler<events::weather::SetWeatherEnabledCommand>(
                [this](const events::weather::SetWeatherEnabledCommand& command)
                {
                    log.push_back("SetWeatherEnabled");
                    if (weatherEnabled && !command.enabled)
                    {
                        // Switching off resets the state and restores the scene's fog.
                        weatherState = ::weather::WeatherState{};
                        weatherProgress = 1.0f;
                        restoreAuthoredFog();
                    }
                    weatherEnabled = command.enabled;
                });
            d.registerCommandHandler<events::weather::SetWeatherImmediateCommand>(
                [this](const events::weather::SetWeatherImmediateCommand& command)
                {
                    log.push_back("SetWeatherImmediate");
                    weatherState = command.state;
                    weatherProgress = 1.0f;
                });
            d.registerCommandHandler<events::weather::SetWeatherPresetCommand>(
                [this](const events::weather::SetWeatherPresetCommand& command)
                {
                    log.push_back("SetWeatherPreset");
                    presetTransitions.push_back(command);
                    weatherProgress = 0.0f; // the blend starts; this fake never advances it
                });
            d.registerCommandHandler<events::weather::UpdateWeatherCommand>(
                [this](const events::weather::UpdateWeatherCommand& command)
                {
                    log.push_back("UpdateWeather");
                    weatherUpdates.push_back(command.deltaTime);
                    if (weatherEnabled)
                    {
                        applyFogWeather();
                    }
                });

            // Undo: PushUndoableCommand stores without executing, like UndoRedoServiceImpl.
            d.registerCommandHandler<events::undoredo::PushUndoableCommand>(
                [this](const events::undoredo::PushUndoableCommand& command)
                {
                    log.push_back("PushUndo");
                    undoStack.push_back(command.command);
                });
            d.registerCommandHandler<events::undoredo::BeginBatchCommand>(
                [this](const events::undoredo::BeginBatchCommand&) { ++batchCommands; });
            d.registerCommandHandler<events::undoredo::EndBatchCommand>(
                [this](const events::undoredo::EndBatchCommand&) { ++batchCommands; });

            d.subscribe<events::scene::RenderSettingsChangedNotification>(
                [this](const events::scene::RenderSettingsChangedNotification& notification)
                {
                    notifications.push_back(notification);
                    log.push_back(std::string("RenderSettingsChanged:") + sectionText(notification.section));
                });
        }
    };

    struct EnvironmentToolFixture
    {
        DispatcherScope dispatcherScope;
        FakeEnvironmentEngine engine;
        mcp::MainThreadQueue queue;
        mcp::ToolRegistry registry;

        EnvironmentToolFixture()
        {
            engine.assets = makeTestDirectory("VertexForge_McpEnvironmentTools");
            engine.registerHandlers();
            const mcp::tools::ToolContext context{queue};
            mcp::tools::registerEnvironmentTools(registry, context);
        }

        ~EnvironmentToolFixture()
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

        // The structured result of a call that must succeed.
        nlohmann::json callOk(const char* name, const nlohmann::json& args)
        {
            const mcp::ToolResult result = call(name, args);
            INFO(name << " failed: " << result.text);
            REQUIRE_FALSE(result.isError);
            return result.structured;
        }

        // The message of the ArgError the call throws; a marker when it throws something else or nothing.
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

        nlohmann::json sceneJson() const
        {
            return {
                {"atmosphere", env::toJson(env::atmosphereTable(), engine.scene.atmosphere)},
                {"clouds", env::toJson(env::cloudTable(), engine.scene.cloud)},
                {"fog", env::toJson(env::fogTable(), engine.scene.postProcess.volumetricFog)},
                {"gi", env::toJson(env::giTable(), engine.scene.gi)}
            };
        }

        nlohmann::json liveJson() const
        {
            return {
                {"atmosphere", env::toJson(env::atmosphereTable(), engine.liveAtmosphere)},
                {"clouds", env::toJson(env::cloudTable(), engine.liveClouds)},
                {"fog", env::toJson(env::fogTable(), engine.livePostProcess.volumetricFog)},
                {"gi", env::toJson(env::giTable(), engine.liveGi)},
                {"giDebug", {engine.liveGi.showProbes, engine.liveGi.showCascadeBounds, engine.liveGi.showProbeValidity}}
            };
        }
    };

    void checkVec3(const glm::vec3& actual, const glm::vec3& expected)
    {
        CHECK(actual.x == doctest::Approx(expected.x));
        CHECK(actual.y == doctest::Approx(expected.y));
        CHECK(actual.z == doctest::Approx(expected.z));
    }

    // A tool name with its arguments.
    struct ToolCall
    {
        const char* name;
        nlohmann::json args;
    };
}

TEST_CASE("mcp environment tools: eleven Main-affinity tools with closed schemas, annotations and timeouts")
{
    EnvironmentToolFixture fixture;
    CHECK(fixture.registry.size() == 11);

    struct Expected
    {
        const char* name;
        bool readOnly;
        long long timeoutMs;
    };
    const std::vector<Expected> expected{
        {"environment_get", true, 10000},
        {"environment_set", false, 30000},
        {"ibl_set", false, 120000},
        {"ibl_bake", false, 120000},
        {"gi_get", true, 10000},
        {"gi_set", false, 60000},
        {"gi_debug", false, 10000},
        {"fog_get", true, 10000},
        {"fog_set", false, 30000},
        {"weather_get", true, 10000},
        {"weather_set", false, 30000}
    };
    for (const Expected& entry : expected)
    {
        CAPTURE(entry.name);
        const mcp::ToolDef& def = fixture.tool(entry.name);
        CHECK(def.affinity == mcp::ThreadAffinity::Main);
        CHECK(def.readOnly == entry.readOnly);
        CHECK_FALSE(def.destructive);
        CHECK(def.timeout.count() == entry.timeoutMs);
        CHECK(schemaValue(def.inputSchema, "type") == "object");
        CHECK(schemaValue(def.inputSchema, "additionalProperties") == false);
    }

    // The nested objects are closed too, and read-only keys are not offered as settable.
    nlohmann::json environmentSchema = fixture.tool("environment_set").inputSchema;
    for (const char* section : {"atmosphere", "clouds", "sun"})
    {
        CAPTURE(section);
        CHECK(schemaValue(environmentSchema["properties"][section], "additionalProperties") == false);
    }
    nlohmann::json atmosphereKeys = environmentSchema["properties"]["atmosphere"]["properties"];
    CHECK(atmosphereKeys.contains("mieAnisotropy"));
    CHECK_FALSE(atmosphereKeys.contains("timeOfDay"));
    CHECK_FALSE(atmosphereKeys.contains("sunAzimuth"));
    CHECK_FALSE(atmosphereKeys.contains("sunElevation"));

    nlohmann::json giSchema = fixture.tool("gi_set").inputSchema;
    CHECK(schemaValue(giSchema["properties"]["settings"], "additionalProperties") == false);
    CHECK(giSchema["properties"]["settings"]["properties"].contains("probeSpacing"));
    CHECK_FALSE(giSchema["properties"]["settings"]["properties"].contains("showProbes"));
    CHECK(giSchema["properties"]["preset"]["enum"] == nlohmann::json::array({"Off", "Medium", "High", "Ultra"}));

    nlohmann::json fogSchema = fixture.tool("fog_set").inputSchema;
    CHECK(fogSchema["properties"].contains("anisotropy"));
    CHECK_FALSE(fogSchema["properties"].contains("earlyTerminationThreshold"));
    CHECK(fogSchema["properties"]["quality"]["enum"] == nlohmann::json::array({"Low", "Medium", "High"}));

    nlohmann::json weatherSchema = fixture.tool("weather_set").inputSchema;
    CHECK(weatherSchema["properties"]["preset"]["enum"] == nlohmann::json::array({
        "Clear", "Cloudy", "Overcast", "LightRain", "HeavyRain", "Thunderstorm", "LightSnow", "HeavySnow", "Fog",
        "Sandstorm"}));

    nlohmann::json iblSchema = fixture.tool("ibl_set").inputSchema;
    CHECK(iblSchema["required"] == nlohmann::json::array({"source"}));
}

TEST_CASE("mcp environment tools: registering the tools dispatches nothing")
{
    DispatcherScope scope; // no handlers at all: any dispatch would throw
    mcp::MainThreadQueue queue;
    mcp::ToolRegistry registry;
    const mcp::tools::ToolContext context{queue};
    CHECK_NOTHROW(mcp::tools::registerEnvironmentTools(registry, context));
    CHECK(registry.size() == 11);
}

TEST_CASE("mcp environment tools: invalid arguments fail the whole call and change nothing")
{
    EnvironmentToolFixture fixture;
    FakeEnvironmentEngine& engine = fixture.engine;
    engine.addLight(3, "Sun", true, glm::vec3(-30.0f, 0.0f, 0.0f));
    const services::TransformData sunBefore = engine.nodes.at(3).transform;
    const nlohmann::json sceneBefore = fixture.sceneJson();
    const nlohmann::json liveBefore = fixture.liveJson();

    SUBCASE("unknown keys name the valid ones")
    {
        const std::string atmosphere = fixture.argError("environment_set", {{"atmosphere", {{"bogusKey", 1}}}});
        CHECK(contains(atmosphere, "unknown atmosphere key 'bogusKey'"));
        CHECK(contains(atmosphere, "planetRadius"));
        CHECK_FALSE(contains(atmosphere, "sunAzimuth")); // read-only keys are not offered
        CHECK(contains(fixture.argError("environment_set", {{"clouds", {{"puffiness", 1}}}}), "unknown clouds key 'puffiness'"));
        CHECK(contains(fixture.argError("environment_set", {{"sun", {{"azimuth", 10}, {"bogus", 1}}}}),
                       "unknown sun key 'bogus'; valid keys: azimuth, elevation, color, intensity, syncEntity"));
        CHECK(contains(fixture.argError("gi_set", {{"settings", {{"bounces", 2}}}}), "unknown settings key 'bounces'"));
    }

    SUBCASE("unknown top-level arguments, for every tool")
    {
        for (const char* name : {"environment_get", "environment_set", "ibl_set", "ibl_bake", "gi_get", "gi_set",
                                 "gi_debug", "fog_get", "fog_set", "weather_get", "weather_set"})
        {
            CAPTURE(name);
            CHECK(contains(fixture.argError(name, {{"foo", 1}}), "unknown argument 'foo'"));
        }
        CHECK(contains(fixture.argError("environment_set", {{"foo", 1}}),
                       "valid arguments: atmosphere, clouds, timeOfDay, sun, createSunIfMissing"));
        CHECK(contains(fixture.argError("weather_get", {{"foo", 1}}), "valid arguments: none"));
    }

    SUBCASE("out-of-range values name the range")
    {
        CHECK(contains(fixture.argError("environment_set", {{"atmosphere", {{"mieAnisotropy", 1.5}}}}),
                       "'atmosphere.mieAnisotropy' must be between -1.0 and 1.0 (got 1.5)"));
        CHECK(contains(fixture.argError("environment_set", {{"clouds", {{"maxMarchSteps", 300}}}}),
                       "'clouds.maxMarchSteps' must be between 16 and 256 (got 300)"));
        CHECK(contains(fixture.argError("environment_set", {{"timeOfDay", 25}}),
                       "'timeOfDay' must be between 0.0 and 24.0 (got 25)"));
        CHECK(contains(fixture.argError("environment_set", {{"sun", {{"intensity", 101}}}}),
                       "'sun.intensity' must be between 0.0 and 100.0 (got 101)"));
        CHECK(contains(fixture.argError("environment_set", {{"atmosphere", {{"groundAlbedo", {0.5, 1.5, 0.5}}}}}),
                       "'atmosphere.groundAlbedo[1]' must be between 0.0 and 1.0 (got 1.5)"));
        // The loader wraps the moon azimuth with fmod(v, 360), so 360 itself is out of range; the bound is the
        // largest float below 360, reported in its shortest form.
        CHECK(contains(fixture.argError("environment_set", {{"atmosphere", {{"moonAzimuth", 360}}}}),
                       "'atmosphere.moonAzimuth' must be between 0.0 and 359.99997 (got 360)"));
        CHECK(contains(fixture.argError("fog_set", {{"anisotropy", 0.995}}),
                       "'anisotropy' must be between -0.99 and 0.99 (got 0.995)"));
        CHECK(contains(fixture.argError("fog_set", {{"noiseOctaves", 5}}), "'noiseOctaves' must be between 1 and 4 (got 5)"));
        // A finite double that does not fit a float never reaches the engine.
        CHECK(contains(fixture.argError("fog_set", {{"uniformDensity", 1.0e39}}), "'uniformDensity' must be between 0.0 and 1.0"));
        CHECK(contains(fixture.argError("gi_set", {{"settings", {{"probeSpacing", 0.5}}}}),
                       "'settings.probeSpacing' must be between 1.0 and 8.0 (got 0.5)"));
        CHECK(contains(fixture.argError("ibl_set", {{"source", "hdr"}, {"intensity", 6}}),
                       "'intensity' must be between 0.0 and 5.0 (got 6)"));
        CHECK(contains(fixture.argError("weather_set", {{"preset", "Fog"}, {"transitionSeconds", 121}}),
                       "'transitionSeconds' must be between 0.0 and 120.0 (got 121)"));
    }

    SUBCASE("wrong types")
    {
        CHECK(contains(fixture.argError("environment_set", {{"atmosphere", {{"enabled", "yes"}}}}),
                       "'atmosphere.enabled' must be a boolean (got \"yes\")"));
        CHECK(contains(fixture.argError("fog_set", {{"uniformDensity", true}}), "'uniformDensity' must be a number (got true)"));
        CHECK(contains(fixture.argError("environment_set", {{"atmosphere", {{"groundAlbedo", {0.1, 0.2}}}}}),
                       "'atmosphere.groundAlbedo' must be an array of 3 numbers"));
        CHECK(contains(fixture.argError("environment_set", {{"clouds", {{"maxMarchSteps", 32.5}}}}),
                       "'clouds.maxMarchSteps' must be an integer (got 32.5)"));
        CHECK(contains(fixture.argError("fog_set", {{"quality", "Ultra"}}),
                       "'quality' must be one of Low, Medium, High (got \"Ultra\")"));
        CHECK(contains(fixture.argError("gi_set", {{"settings", {{"quality", 2}}}}),
                       "'settings.quality' must be one of Off, Medium, High, Ultra (got 2)"));
        CHECK(contains(fixture.argError("environment_set", {{"atmosphere", 5}}), "'atmosphere' must be an object (got 5)"));
        CHECK(contains(fixture.argError("environment_set", {{"sun", "noon"}}), "'sun' must be an object"));
        CHECK(contains(fixture.argError("gi_debug", {{"probes", "yes"}}), "'probes' must be a boolean"));
        CHECK(contains(fixture.argError("weather_set", {{"enabled", 1}}), "'enabled' must be a boolean (got 1)"));
    }

    SUBCASE("read-only keys with a new value say what sets them")
    {
        CHECK(contains(fixture.argError("environment_set", {{"atmosphere", {{"timeOfDay", 13}}}}),
                       "'atmosphere.timeOfDay' is read-only here: set it with the timeOfDay argument"));
        CHECK(contains(fixture.argError("environment_set", {{"atmosphere", {{"sunAzimuth", 10}}}}),
                       "'atmosphere.sunAzimuth' is read-only here: set it with sun.azimuth or timeOfDay"));
        CHECK(contains(fixture.argError("environment_set", {{"atmosphere", {{"sunElevation", 10}}}}),
                       "'atmosphere.sunElevation' is read-only here: set it with sun.elevation or timeOfDay"));
        // The GI debug views are live-only: refused even when the value equals the saved one (false), since
        // no *_get reports them and gi_debug is the way to change them.
        for (const char* key : {"showProbes", "showCascadeBounds", "showProbeValidity"})
        {
            CAPTURE(key);
            CHECK(contains(fixture.argError("gi_set", {{"settings", {{key, true}}}}), "is read-only here: use gi_debug"));
            CHECK(contains(fixture.argError("gi_set", {{"settings", {{key, false}}}}), "is read-only here: use gi_debug"));
        }
    }

    SUBCASE("cross-field rules")
    {
        CHECK(contains(fixture.argError("environment_set", {{"atmosphere", {{"planetRadius", 7.0e6}}}}),
                       "atmosphere: atmosphereRadius (6460000.0) must be greater than planetRadius (7000000.0)"));
        CHECK(contains(fixture.argError("environment_set", {{"clouds", {{"cloudMinAltitude", 5000}}}}),
                       "clouds: cloudMinAltitude (5000.0) must be below cloudMaxAltitude (4000.0)"));
    }

    SUBCASE("conflicting or missing arguments")
    {
        CHECK(contains(fixture.argError("environment_set", {{"timeOfDay", 17.85}, {"sun", {{"azimuth", 90}}}}),
                       "pass either timeOfDay or sun.azimuth / sun.elevation, not both"));
        CHECK(contains(fixture.argError("environment_set", {{"timeOfDay", 6}, {"atmosphere", {{"moonAzimuth", 10}}}}),
                       "timeOfDay places the moon itself"));
        CHECK(contains(fixture.argError("weather_set", {{"preset", "Fog"}, {"enabled", false}}),
                       "cannot be combined with enabled:false"));
        CHECK(contains(fixture.argError("weather_set", {{"transitionSeconds", 5}}), "argument 'transitionSeconds' needs 'preset'"));
        CHECK(contains(fixture.argError("ibl_set", {{"source", "sky"}}),
                       "argument 'source' must be one of hdr, dynamicSky, none (got 'sky')"));
        CHECK(contains(fixture.argError("ibl_set", {{"source", "none"}, {"path", "skies/sky.vfHdr"}}),
                       "argument 'path' does not apply to source 'none'"));
        CHECK(contains(fixture.argError("ibl_set", {{"source", "dynamicSky"}, {"tint", {1.0, 1.0, 1.0}}}),
                       "argument 'tint' does not apply to source 'dynamicSky'"));
        CHECK(contains(fixture.argError("ibl_set", {{"source", "hdr"}, {"createSunIfMissing", true}}),
                       "argument 'createSunIfMissing' does not apply to source 'hdr'"));
        CHECK(contains(fixture.argError("ibl_bake", {{"target", "environment"}, {"entity", 42}}),
                       "it needs target 'probes' or 'all'"));
        for (const char* name : {"environment_set", "gi_set", "fog_set", "weather_set", "gi_debug"})
        {
            CAPTURE(name);
            CHECK(contains(fixture.argError(name, nlohmann::json::object()), "nothing to change"));
        }
    }

    CHECK(fixture.sceneJson() == sceneBefore);
    CHECK(fixture.liveJson() == liveBefore);
    CHECK(engine.nodes.at(3).transform == sunBefore);
    CHECK_FALSE(engine.rootIbl.has_value());
    CHECK_FALSE(engine.weatherEnabled);
    checkLog(engine.log, {});
    CHECK(engine.undoStack.empty());
    CHECK(engine.notifications.empty());
    CHECK(engine.batchCommands == 0);
}

TEST_CASE("mcp environment tools: a value a *_get reports is accepted back unchanged")
{
    EnvironmentToolFixture fixture;
    FakeEnvironmentEngine& engine = fixture.engine;
    engine.scene.postProcess.volumetricFog.uniformDensity = 0.05f;
    engine.livePostProcess.volumetricFog.uniformDensity = 0.05f;

    nlohmann::json got = fixture.callOk("fog_get", nlohmann::json::object());
    const nlohmann::json reported = got["authored"]["uniformDensity"];
    CHECK(reported.dump() == "0.05"); // the shortest round-trip form, not 0.05000000074505806
    CHECK(got["effective"]["uniformDensity"].dump() == "0.05");

    nlohmann::json set = fixture.callOk("fog_set", {{"uniformDensity", reported}, {"heightFogDensity", 0.3}});
    CHECK(set["changed"] == true);
    CHECK(set["diff"] == nlohmann::json{{"heightFogDensity", 0.3}}); // the reported value is unchanged
    CHECK(set["effective"]["uniformDensity"].dump() == "0.05");
    CHECK(engine.scene.postProcess.volumetricFog.uniformDensity == 0.05f);

    // The bounds are inclusive after narrowing to float, so the tables' own limits are accepted.
    CHECK_FALSE(fixture.call("fog_set", {{"maxDistance", 5000}, {"noiseScale", 0.001}}).isError);
    CHECK_FALSE(fixture.call("fog_set", {{"anisotropy", 0.99000000001}}).isError);
    CHECK_FALSE(fixture.call("environment_set", {{"atmosphere", {{"sunAngularRadius", 0.08727}, {"moonAzimuth", 359.99}}}}).isError);
    CHECK(engine.scene.atmosphere.sunAngularRadius == static_cast<float>(0.08727));
    CHECK(engine.scene.postProcess.volumetricFog.anisotropy == static_cast<float>(0.99));
}

TEST_CASE("mcp environment tools: a whole *_get object passed back to its *_set changes nothing")
{
    EnvironmentToolFixture fixture;
    FakeEnvironmentEngine& engine = fixture.engine;

    // Read-only keys (timeOfDay, the sun angles) echoing their current value are skipped.
    nlohmann::json environment = fixture.callOk("environment_get", nlohmann::json::object());
    nlohmann::json sameEnvironment = fixture.callOk("environment_set", {
        {"atmosphere", environment["atmosphere"]}, {"clouds", environment["clouds"]}});
    CHECK(sameEnvironment["changed"] == false);

    nlohmann::json fog = fixture.callOk("fog_get", nlohmann::json::object());
    CHECK(fixture.callOk("fog_set", fog["authored"])["changed"] == false);

    nlohmann::json gi = fixture.callOk("gi_get", nlohmann::json::object());
    CHECK(fixture.callOk("gi_set", {{"settings", gi["authored"]}})["changed"] == false);

    // 12 echoes the saved 12.0.
    CHECK(fixture.callOk("environment_set", {{"atmosphere", {{"timeOfDay", 12}}}})["changed"] == false);
    checkLog(engine.log, {});
    CHECK(engine.undoStack.empty());

    // The current moon angles may accompany timeOfDay, which then places the moon itself.
    nlohmann::json afternoon = fixture.callOk("environment_set", {
        {"timeOfDay", 15.0}, {"atmosphere", {{"moonAzimuth", 180.0}, {"moonElevation", -45.0}}}});
    CHECK(afternoon["changed"] == true);
    CHECK(engine.scene.atmosphere.moonAzimuth == doctest::Approx(135.0f)); // (315 + 180) mod 360
    CHECK(engine.scene.atmosphere.moonAzimuth == doctest::Approx(dayNightAngles(15.0f, 0.0f).moonAzimuth));
}

TEST_CASE("mcp environment tools: echoing a *_get object under weather warns only about keys that change")
{
    EnvironmentToolFixture fixture;
    FakeEnvironmentEngine& engine = fixture.engine;
    engine.weatherEnabled = true;
    engine.weatherState = ::weather::getPreset(::weather::WeatherPresetId::Fog);
    engine.applyFogWeather();

    const auto hasWeatherWarning = [](const nlohmann::json& out)
    {
        for (const nlohmann::json& warning : out["warnings"])
        {
            if (warning.get<std::string>().find("Weather is enabled and overrides") != std::string::npos)
            {
                return true;
            }
        }
        return false;
    };

    // The echoed objects hold every weather-driven key, but none of them changes.
    nlohmann::json environment = fixture.callOk("environment_get", nlohmann::json::object());
    nlohmann::json sameEnvironment = fixture.callOk("environment_set", {
        {"atmosphere", environment["atmosphere"]}, {"clouds", environment["clouds"]}});
    CHECK(sameEnvironment["changed"] == false);
    CHECK_FALSE(hasWeatherWarning(sameEnvironment));

    nlohmann::json fog = fixture.callOk("fog_get", nlohmann::json::object());
    nlohmann::json sameFog = fixture.callOk("fog_set", fog["authored"]);
    CHECK(sameFog["changed"] == false);
    CHECK_FALSE(hasWeatherWarning(sameFog));

    // A weather-driven key that does change still warns.
    nlohmann::json changedFog = fixture.callOk("fog_set", {{"uniformDensity", 0.4}});
    CHECK(hasWeatherWarning(changedFog));
}

TEST_CASE("mcp environment tools: environment_set timeOfDay places sun and moon, rotates the sun light, one undo step")
{
    EnvironmentToolFixture fixture;
    FakeEnvironmentEngine& engine = fixture.engine;
    engine.scene.atmosphere.enabled = true;
    engine.scene.atmosphere.moonPhaseOffset = 0.25f;
    engine.liveAtmosphere = engine.scene.atmosphere;
    engine.addLight(3, "Sun", true, glm::vec3(-45.0f, 30.0f, 0.0f));
    const services::TransformData transformBefore = engine.nodes.at(3).transform;
    const nlohmann::json atmosphereBefore = env::toJson(env::atmosphereTable(), engine.scene.atmosphere);

    nlohmann::json out = fixture.callOk("environment_set", {{"timeOfDay", 17.85}});

    // 17.85 h = 9 minutes before sunset: the sun a few degrees above the horizon.
    const ::render::atmosphere::DayNightAngles angles = dayNightAngles(17.85f, 0.25f);
    CHECK(angles.sunAzimuth == doctest::Approx(357.75f));
    CHECK(angles.sunElevation == doctest::Approx(3.5334f).epsilon(0.001));
    CHECK(angles.moonAzimuth == doctest::Approx(267.75f)); // 357.75 + 180 + 0.25 * 360, wrapped

    const env::AtmosphereSettings& atmosphere = engine.scene.atmosphere;
    CHECK(atmosphere.timeOfDay == doctest::Approx(17.85f));
    CHECK(atmosphere.sunAzimuth == doctest::Approx(angles.sunAzimuth));
    CHECK(atmosphere.sunElevation == doctest::Approx(angles.sunElevation));
    CHECK(atmosphere.moonAzimuth == doctest::Approx(angles.moonAzimuth));
    CHECK(atmosphere.moonElevation == doctest::Approx(angles.moonElevation));
    CHECK(env::toJson(env::atmosphereTable(), engine.liveAtmosphere) == env::toJson(env::atmosphereTable(), atmosphere));

    // Only the rotation of the sun light changes, to the exact Euler of the new sun angles.
    const glm::vec3 euler = directionalLightEulerForSun(atmosphere.sunAzimuth, atmosphere.sunElevation);
    const services::TransformData& transform = engine.nodes.at(3).transform;
    checkVec3(transform.rotation, euler);
    CHECK(transform.position == transformBefore.position);
    CHECK(transform.scale == transformBefore.scale);

    // The live apply went through the no-Sun command; one batch, one notification.
    checkLog(engine.log, {"SetRenderSettings", "ApplyAtmosphereSettingsNoSun", "RenderSettingsChanged:Atmosphere",
                          "SetTransform", "PushUndo"});
    REQUIRE(engine.notifications.size() == 1);
    CHECK(engine.notifications[0].section == events::scene::RenderSettingsSection::Atmosphere);
    CHECK_FALSE(engine.notifications[0].sceneLoad);
    REQUIRE(engine.undoStack.size() == 1);
    CHECK(engine.undoStack[0]->getDescription() == "MCP: Set environment");
    CHECK(batchSize(engine.undoStack[0]) == 2);

    CHECK(out["changed"] == true);
    CHECK(out["timeOfDay"] == 17.85);
    CHECK(out["atmosphere"].size() == 5); // timeOfDay and the four sun / moon angles
    CHECK(out["atmosphere"].contains("sunAzimuth"));
    CHECK(out["atmosphere"].contains("moonElevation"));
    CHECK(out["clouds"].empty());
    CHECK(out["sun"]["entity"] == 3);
    CHECK(out["sun"]["rotated"] == true);
    CHECK(out["sun"]["created"] == false);
    CHECK(out["sun"]["lightChanged"] == false);
    CHECK(out["warnings"].empty());

    // Undo restores the atmosphere and the light, newest step first.
    engine.log.clear();
    engine.undoStack[0]->undo();
    CHECK(env::toJson(env::atmosphereTable(), engine.scene.atmosphere) == atmosphereBefore);
    CHECK(env::toJson(env::atmosphereTable(), engine.liveAtmosphere) == atmosphereBefore);
    CHECK(engine.nodes.at(3).transform == transformBefore);
    checkLog(engine.log, {"SetTransform", "SetRenderSettings", "ApplyAtmosphereSettingsNoSun",
                          "RenderSettingsChanged:Atmosphere"});

    // Redo re-applies both.
    engine.log.clear();
    engine.undoStack[0]->execute();
    CHECK(engine.scene.atmosphere.timeOfDay == doctest::Approx(17.85f));
    CHECK(engine.liveAtmosphere.sunAzimuth == doctest::Approx(angles.sunAzimuth));
    checkVec3(engine.nodes.at(3).transform.rotation, euler);
    checkLog(engine.log, {"SetRenderSettings", "ApplyAtmosphereSettingsNoSun", "RenderSettingsChanged:Atmosphere",
                          "SetTransform"});
}

TEST_CASE("mcp environment tools: environment_set sun angles, light and syncEntity drive the active sun light")
{
    EnvironmentToolFixture fixture;
    FakeEnvironmentEngine& engine = fixture.engine;
    engine.addLight(3, "Sun", true, glm::vec3(-45.0f, 0.0f, 0.0f));
    const services::TransformData transformBefore = engine.nodes.at(3).transform;

    SUBCASE("angles and light colour in one step")
    {
        nlohmann::json out = fixture.callOk("environment_set", {{"sun", {
            {"azimuth", 120.0}, {"elevation", 25.0}, {"color", {1.0, 0.8, 0.6}}, {"intensity", 3.0}}}});
        CHECK(engine.scene.atmosphere.sunAzimuth == 120.0f);
        CHECK(engine.scene.atmosphere.sunElevation == 25.0f);
        CHECK(engine.scene.atmosphere.timeOfDay == 12.0f);    // sun angles do not move the clock ...
        CHECK(engine.scene.atmosphere.moonAzimuth == 180.0f); // ... or the moon
        const glm::vec2 angles = sunAnglesFromDirectionalLightEuler(engine.nodes.at(3).transform.rotation);
        CHECK(angles.x == doctest::Approx(120.0f).epsilon(1e-4));
        CHECK(angles.y == doctest::Approx(25.0f).epsilon(1e-4));
        REQUIRE(engine.nodes.at(3).light.has_value());
        CHECK(engine.nodes.at(3).light->color == glm::vec3(1.0f, 0.8f, 0.6f));
        CHECK(engine.nodes.at(3).light->intensity == 3.0f);
        CHECK(out["sun"]["rotated"] == true);
        CHECK(out["sun"]["lightChanged"] == true);
        checkLog(engine.log, {"SetRenderSettings", "ApplyAtmosphereSettingsNoSun", "RenderSettingsChanged:Atmosphere",
                              "SetTransform", "SetDirectionalLightData", "PushUndo"});
        REQUIRE(engine.undoStack.size() == 1);
        CHECK(batchSize(engine.undoStack[0]) == 3);

        engine.undoStack[0]->undo();
        CHECK(engine.nodes.at(3).transform == transformBefore);
        CHECK(engine.nodes.at(3).light->color == glm::vec3(1.0f));
        CHECK(engine.nodes.at(3).light->intensity == 1.0f);
        CHECK(engine.scene.atmosphere.sunAzimuth == 0.0f);
        CHECK(engine.scene.atmosphere.sunElevation == 45.0f);
    }

    SUBCASE("colour / intensity alone change the light without rotating it")
    {
        nlohmann::json out = fixture.callOk("environment_set", {{"sun", {{"intensity", 2.5}}}});
        checkLog(engine.log, {"SetDirectionalLightData", "PushUndo"});
        CHECK(engine.nodes.at(3).transform == transformBefore);
        REQUIRE(engine.nodes.at(3).light.has_value());
        CHECK(engine.nodes.at(3).light->intensity == 2.5f);
        CHECK(engine.nodes.at(3).light->color == glm::vec3(1.0f)); // fields not passed are kept
        CHECK(out["sun"]["rotated"] == false);
        CHECK(out["sun"]["lightChanged"] == true);
        CHECK(out["atmosphere"].empty());
        REQUIRE(engine.undoStack.size() == 1);
        CHECK(batchSize(engine.undoStack[0]) == 1);

        engine.undoStack[0]->undo();
        CHECK(engine.nodes.at(3).light->intensity == 1.0f);
    }

    SUBCASE("syncEntity:true alone re-aims the light at the saved angles")
    {
        engine.scene.atmosphere.sunAzimuth = 200.0f;
        engine.scene.atmosphere.sunElevation = 10.0f;
        nlohmann::json out = fixture.callOk("environment_set", {{"sun", {{"syncEntity", true}}}});
        checkLog(engine.log, {"SetTransform", "PushUndo"});
        const glm::vec2 angles = sunAnglesFromDirectionalLightEuler(engine.nodes.at(3).transform.rotation);
        CHECK(angles.x == doctest::Approx(200.0f).epsilon(1e-4));
        CHECK(angles.y == doctest::Approx(10.0f).epsilon(1e-4));
        CHECK(out["atmosphere"].empty());
        CHECK(out["sun"]["rotated"] == true);
    }

    SUBCASE("syncEntity:false keeps the light and warns that the new angles are not visible")
    {
        nlohmann::json out = fixture.callOk("environment_set", {{"timeOfDay", 9.0}, {"sun", {{"syncEntity", false}}}});
        CHECK(engine.nodes.at(3).transform == transformBefore);
        CHECK(out["sun"]["rotated"] == false);
        REQUIRE(out["warnings"].size() == 1);
        CHECK(contains(out["warnings"][0].get<std::string>(), "sun.syncEntity is false"));
        checkLog(engine.log, {"SetRenderSettings", "ApplyAtmosphereSettingsNoSun", "RenderSettingsChanged:Atmosphere",
                              "PushUndo"});
    }

    SUBCASE("an inactive light is reported, never changed or duplicated")
    {
        engine.nodes.at(3).active = false;
        nlohmann::json out = fixture.callOk("environment_set", {{"timeOfDay", 9.0}, {"atmosphere", {{"enabled", true}}}});
        CHECK(engine.nodes.at(3).transform == transformBefore);
        REQUIRE(out["warnings"].size() == 1);
        CHECK(contains(out["warnings"][0].get<std::string>(), "is inactive"));
        CHECK(indexOf(engine.log, "CreateEntity") == -1);
        CHECK(indexOf(engine.log, "SetTransform") == -1);
        CHECK(out["sun"]["created"] == false);
    }
}

TEST_CASE("mcp environment tools: enabling the atmosphere without a directional light creates an undoable Sun")
{
    EnvironmentToolFixture fixture;
    FakeEnvironmentEngine& engine = fixture.engine;
    engine.addEntity(1, "Floor", FakeEnvironmentEngine::rootId);
    const nlohmann::json atmosphereBefore = env::toJson(env::atmosphereTable(), engine.scene.atmosphere);

    SUBCASE("created in one step; undo deletes it and restores the atmosphere; redo brings it back")
    {
        nlohmann::json out = fixture.callOk("environment_set", {{"atmosphere", {{"enabled", true}}}});
        checkLog(engine.log, {"SetRenderSettings", "ApplyAtmosphereSettingsNoSun", "RenderSettingsChanged:Atmosphere",
                              "CreateEntity", "AddDirectionalLight", "SetTransform", "PushUndo"});

        const std::optional<uint64_t> created = engine.findByName("Sun");
        REQUIRE(created.has_value());
        CHECK(out["sun"]["created"] == true);
        CHECK(out["sun"]["entity"] == *created);
        const FakeEnvironmentEngine::Node& node = engine.nodes.at(*created);
        CHECK(node.parent == std::optional<uint64_t>(FakeEnvironmentEngine::rootId));
        CHECK(node.light.has_value());
        const glm::vec3 euler = directionalLightEulerForSun(engine.scene.atmosphere.sunAzimuth,
                                                            engine.scene.atmosphere.sunElevation);
        checkVec3(node.transform.rotation, euler);
        CHECK(engine.scene.atmosphere.enabled);
        CHECK(engine.liveAtmosphere.enabled);
        REQUIRE(engine.undoStack.size() == 1);
        CHECK(batchSize(engine.undoStack[0]) == 2);

        // Newest first: the Sun goes, then the atmosphere is restored.
        engine.log.clear();
        engine.undoStack[0]->undo();
        CHECK_FALSE(engine.alive(*created));
        CHECK_FALSE(engine.findByName("Sun").has_value());
        CHECK(env::toJson(env::atmosphereTable(), engine.scene.atmosphere) == atmosphereBefore);
        CHECK_FALSE(engine.liveAtmosphere.enabled);
        checkLog(engine.log, {"DeleteEntity", "SetRenderSettings", "ApplyAtmosphereSettingsNoSun",
                              "RenderSettingsChanged:Atmosphere"});

        // Redo re-creates it from its snapshot under a new id that the old one resolves to.
        engine.undoStack[0]->execute();
        const std::optional<uint64_t> recreated = engine.findByName("Sun");
        REQUIRE(recreated.has_value());
        CHECK(*recreated != *created);
        CHECK(mcp::undo::EntityIdRemap::instance().resolve(static_cast<uint32_t>(*created)) == *recreated);
        CHECK(engine.nodes.at(*recreated).light.has_value());
        checkVec3(engine.nodes.at(*recreated).transform.rotation, euler);
        CHECK(engine.scene.atmosphere.enabled);
        CHECK(engine.liveAtmosphere.enabled);
        CHECK(indexOf(engine.log, "ApplyAtmosphereSettings") == -1);
    }

    SUBCASE("createSunIfMissing:false leaves the scene without one")
    {
        nlohmann::json out = fixture.callOk("environment_set", {{"atmosphere", {{"enabled", true}}}, {"createSunIfMissing", false}});
        CHECK_FALSE(engine.findByName("Sun").has_value());
        CHECK(indexOf(engine.log, "CreateEntity") == -1);
        CHECK(out["sun"]["created"] == false);
        CHECK(out["sun"]["entity"].is_null());
        CHECK(engine.scene.atmosphere.enabled);
        CHECK(engine.undoStack.size() == 1);
    }

    SUBCASE("an inactive directional light counts: no second Sun")
    {
        engine.addLight(3, "Moonlight", false, glm::vec3(0.0f));
        fixture.callOk("environment_set", {{"atmosphere", {{"enabled", true}}}});
        CHECK_FALSE(engine.findByName("Sun").has_value());
        CHECK(indexOf(engine.log, "CreateEntity") == -1);
    }

    SUBCASE("a failure part-way rolls the whole edit back")
    {
        engine.lightAddSucceeds = false;
        const mcp::ToolResult result = fixture.call("environment_set", {{"atmosphere", {{"enabled", true}}}});
        CHECK(result.isError);
        CHECK(contains(result.text, "rolled back"));
        CHECK(contains(result.text, "DirectionalLight"));
        CHECK_FALSE(engine.findByName("Sun").has_value());
        CHECK(env::toJson(env::atmosphereTable(), engine.scene.atmosphere) == atmosphereBefore);
        CHECK_FALSE(engine.liveAtmosphere.enabled);
        CHECK(engine.undoStack.empty());
        checkLog(engine.log, {"SetRenderSettings", "ApplyAtmosphereSettingsNoSun", "RenderSettingsChanged:Atmosphere",
                              "CreateEntity", "AddDirectionalLight", "DeleteEntity",
                              "SetRenderSettings", "ApplyAtmosphereSettingsNoSun", "RenderSettingsChanged:Atmosphere"});
    }
}

TEST_CASE("mcp environment tools: without an open scene an edit fails and pushes nothing")
{
    EnvironmentToolFixture fixture;
    FakeEnvironmentEngine& engine = fixture.engine;
    engine.storeSucceeds = false;
    const nlohmann::json liveBefore = fixture.liveJson();

    const mcp::ToolResult result = fixture.call("fog_set", {{"enabled", true}});
    CHECK(result.isError);
    CHECK(contains(result.text, "rolled back"));
    CHECK(contains(result.text, "no scene is open"));
    CHECK(fixture.liveJson() == liveBefore);
    CHECK(engine.undoStack.empty());
    CHECK(engine.notifications.empty());
    // The write, then the rollback's attempt to restore it: both refused before any live apply.
    checkLog(engine.log, {"SetRenderSettings", "SetRenderSettings"});
}

TEST_CASE("mcp environment tools: undoing one section keeps a later edit of another section")
{
    EnvironmentToolFixture fixture;
    FakeEnvironmentEngine& engine = fixture.engine;

    fixture.callOk("environment_set", {{"atmosphere", {{"mieAnisotropy", 0.5}}}});
    fixture.callOk("gi_set", {{"settings", {{"probeSpacing", 3.0}}}});
    REQUIRE(engine.undoStack.size() == 2);
    CHECK(engine.scene.atmosphere.mieAnisotropy == 0.5f);
    CHECK(engine.scene.gi.probeSpacing == 3.0f);

    // Undo the atmosphere edit while the later GI edit stands: the scene settings are re-read, not restored
    // from a snapshot taken before the GI edit.
    engine.undoStack[0]->undo();
    CHECK(engine.scene.atmosphere.mieAnisotropy == 0.8f);
    CHECK(engine.liveAtmosphere.mieAnisotropy == 0.8f);
    CHECK(engine.scene.gi.probeSpacing == 3.0f);
    CHECK(engine.liveGi.probeSpacing == 3.0f);

    // And the other way round.
    engine.undoStack[0]->execute();
    engine.undoStack[1]->undo();
    CHECK(engine.scene.gi.probeSpacing == 2.0f);
    CHECK(engine.scene.atmosphere.mieAnisotropy == 0.5f);
}

TEST_CASE("mcp environment tools: Play mode and a pending scene load refuse the *_set tools only")
{
    EnvironmentToolFixture fixture;
    FakeEnvironmentEngine& engine = fixture.engine;
    touch(engine.assets / "skies" / "sky.vfHdr");
    const nlohmann::json sceneBefore = fixture.sceneJson();
    const nlohmann::json liveBefore = fixture.liveJson();

    const std::vector<ToolCall> edits{
        {"environment_set", {{"timeOfDay", 12.0}}},
        {"ibl_set", {{"source", "hdr"}, {"path", "skies/sky.vfHdr"}}},
        {"gi_set", {{"preset", "High"}}},
        {"fog_set", {{"enabled", true}}},
        {"weather_set", {{"preset", "Fog"}}}
    };
    const auto checkRefused = [&fixture, &engine, &edits, &sceneBefore, &liveBefore](const char* reason)
    {
        CAPTURE(reason);
        for (const ToolCall& edit : edits)
        {
            CAPTURE(edit.name);
            const mcp::ToolResult result = fixture.call(edit.name, edit.args);
            CHECK(result.isError);
            CHECK(contains(result.text, reason));
        }
        CHECK(fixture.sceneJson() == sceneBefore);
        CHECK(fixture.liveJson() == liveBefore);
        CHECK_FALSE(engine.rootIbl.has_value());
        CHECK_FALSE(engine.weatherEnabled);
        checkLog(engine.log, {});
        CHECK(engine.undoStack.empty());
    };

    SUBCASE("Play mode")
    {
        engine.playMode = true;
        checkRefused("Play mode");
        CHECK(contains(fixture.call("fog_set", {{"enabled", true}}).text, "play_stop"));

        // Reading, the GI debug views and baking stay available.
        CHECK_FALSE(fixture.call("environment_get", nlohmann::json::object()).isError);
        CHECK_FALSE(fixture.call("gi_get", nlohmann::json::object()).isError);
        CHECK_FALSE(fixture.call("fog_get", nlohmann::json::object()).isError);
        CHECK_FALSE(fixture.call("weather_get", nlohmann::json::object()).isError);
        CHECK_FALSE(fixture.call("gi_debug", {{"probes", true}}).isError);
        CHECK_FALSE(fixture.call("ibl_bake", {{"target", "probes"}}).isError);
        checkLog(engine.log, {"SetGIDebugProbes", "BakeReflectionProbes"});
        CHECK(engine.liveGi.showProbes);
        CHECK(engine.undoStack.empty());
    }

    SUBCASE("scene load pending")
    {
        engine.loadPending = true;
        checkRefused("scene load is in progress");
    }

    SUBCASE("no scene persistence service means no load is pending")
    {
        events::EventDispatcher::instance().unregisterQueryHandler<events::scene::IsSceneLoadPendingQuery>();
        CHECK_FALSE(fixture.call("fog_set", {{"enabled", true}}).isError);
        CHECK(engine.scene.postProcess.volumetricFog.enabled);
    }
}

TEST_CASE("mcp environment tools: fog_set changes only the volumetric fog")
{
    EnvironmentToolFixture fixture;
    FakeEnvironmentEngine& engine = fixture.engine;
    engine.livePostProcess.bloom.enabled = true;
    engine.livePostProcess.bloom.intensity = 0.9f;
    engine.livePostProcess.upscale.enabled = true;
    engine.scene.postProcess.bloom.intensity = 0.3f;
    const env::FogSettings fogBefore = engine.scene.postProcess.volumetricFog;

    nlohmann::json out = fixture.callOk("fog_set", {
        {"enabled", true}, {"quality", "high"}, {"heightFogDensity", 0.08}, {"heightFogFalloff", 0.2},
        {"anisotropy", 0.8}, {"scatteringCoefficient", 1}});

    const env::FogSettings& fog = engine.scene.postProcess.volumetricFog;
    CHECK(fog.enabled);
    CHECK(fog.quality == ::postprocess::VolumetricQuality::High); // matched case-insensitively
    CHECK(fog.heightFogDensity == doctest::Approx(0.08f));
    CHECK(fog.heightFogFalloff == doctest::Approx(0.2f));
    CHECK(fog.anisotropy == doctest::Approx(0.8f));
    CHECK(fog.scatteringCoefficient == 1.0f);
    CHECK(fog.uniformDensity == fogBefore.uniformDensity);
    CHECK(env::toJson(env::fogTable(), engine.livePostProcess.volumetricFog) == env::toJson(env::fogTable(), fog));

    // Everything outside volumetricFog is untouched, live and saved.
    CHECK(engine.livePostProcess.bloom.enabled);
    CHECK(engine.livePostProcess.bloom.intensity == 0.9f);
    CHECK(engine.livePostProcess.upscale.enabled);
    CHECK(engine.scene.postProcess.bloom.intensity == 0.3f);
    CHECK_FALSE(engine.scene.postProcess.bloom.enabled);
    CHECK_FALSE(engine.scene.postProcess.upscale.enabled);

    CHECK(out["changed"] == true);
    CHECK(out["diff"].size() == 6);
    CHECK(out["diff"]["quality"] == "High");
    CHECK(out["effective"]["quality"] == "High");
    checkLog(engine.log, {"SetRenderSettings", "ApplyPostProcessSettings", "RenderSettingsChanged:VolumetricFog",
                          "PushUndo"});
    REQUIRE(engine.undoStack.size() == 1);
    CHECK(engine.undoStack[0]->getDescription() == "MCP: Set fog");

    engine.undoStack[0]->undo();
    CHECK(env::toJson(env::fogTable(), engine.scene.postProcess.volumetricFog) == env::toJson(env::fogTable(), fogBefore));
    CHECK(env::toJson(env::fogTable(), engine.livePostProcess.volumetricFog) == env::toJson(env::fogTable(), fogBefore));
    CHECK(engine.livePostProcess.bloom.intensity == 0.9f);
    CHECK(engine.livePostProcess.upscale.enabled);

    // earlyTerminationThreshold is not saved with the scene, so fog_set does not take it.
    CHECK(contains(fixture.argError("fog_set", {{"earlyTerminationThreshold", 0.02}}),
                   "unknown argument 'earlyTerminationThreshold'"));
}

TEST_CASE("mcp environment tools: fog_set under weather saves the fog while weather keeps the live density")
{
    EnvironmentToolFixture fixture;
    FakeEnvironmentEngine& engine = fixture.engine;
    engine.weatherEnabled = true;
    engine.weatherState = ::weather::getPreset(::weather::WeatherPresetId::Fog);
    engine.applyFogWeather();

    nlohmann::json out = fixture.callOk("fog_set", {{"enabled", true}, {"uniformDensity", 0.3}});
    // The write replaced the live fog; weather is re-applied right away so 'effective' is what renders.
    checkLog(engine.log, {"SetRenderSettings", "ApplyPostProcessSettings", "RenderSettingsChanged:VolumetricFog",
                          "PushUndo", "UpdateWeather"});
    CHECK(engine.scene.postProcess.volumetricFog.uniformDensity == doctest::Approx(0.3f));
    CHECK(engine.livePostProcess.volumetricFog.uniformDensity == 0.1f);
    CHECK(engine.livePostProcess.volumetricFog.enabled);
    CHECK(out["diff"]["uniformDensity"] == 0.3);
    CHECK(out["effective"]["uniformDensity"] == 0.1);
    REQUIRE(out["warnings"].size() == 1);
    CHECK(contains(out["warnings"][0].get<std::string>(), "overrides uniformDensity live"));
}

TEST_CASE("mcp environment tools: fog_get reports the saved fog, the live fog weather drives and derived visibility")
{
    EnvironmentToolFixture fixture;
    FakeEnvironmentEngine& engine = fixture.engine;
    engine.scene.postProcess.volumetricFog.enabled = true;
    engine.livePostProcess.volumetricFog = engine.scene.postProcess.volumetricFog;

    SUBCASE("weather off: effective = authored")
    {
        nlohmann::json out = fixture.callOk("fog_get", nlohmann::json::object());
        CHECK(out["authored"] == out["effective"]);
        CHECK(out["weather"]["enabled"] == false);
        CHECK(out["weather"]["overrides"].empty());
        // (scattering 0.5 + absorption 0.1) * (uniform 0.02 + height 0.05) = 0.042 per metre; ln(20) / 0.042 =
        // 71 m, well inside the 500 m the fog covers.
        CHECK(out["derived"]["extinctionAtGround"].get<double>() == doctest::Approx(0.042));
        CHECK(out["derived"]["visibilityMeters"].get<double>() == doctest::Approx(std::log(20.0) / 0.042));
        CHECK_FALSE(out["derived"].contains("limitedByMaxDistance"));
        CHECK(out["ranges"]["anisotropy"] == nlohmann::json{{"type", "number"}, {"min", -0.99}, {"max", 0.99}});
        CHECK(out["ranges"]["uniformDensity"]["weatherDriven"] == true);
        CHECK_FALSE(out["ranges"].contains("earlyTerminationThreshold"));
        CHECK(out["notes"].is_array());
        CHECK_FALSE(out["notes"].empty());
        CHECK(out["warnings"].empty());

        nlohmann::json withoutRanges = fixture.callOk("fog_get", {{"includeRanges", false}});
        CHECK_FALSE(withoutRanges.contains("ranges"));
    }

    SUBCASE("weather on: effective is what weather drives")
    {
        engine.weatherEnabled = true;
        engine.weatherState = ::weather::getPreset(::weather::WeatherPresetId::Fog);
        engine.applyFogWeather();

        const mcp::ToolResult result = fixture.call("fog_get", nlohmann::json::object());
        REQUIRE_FALSE(result.isError);
        nlohmann::json out = result.structured;
        CHECK(out["authored"]["uniformDensity"] == 0.02);
        CHECK(out["authored"]["heightFogDensity"] == 0.05);
        CHECK(out["effective"]["uniformDensity"] == 0.1);
        CHECK(out["effective"]["heightFogDensity"] == 0.15);
        CHECK(out["effective"]["fogColor"] == nlohmann::json::array({0.88, 0.9, 0.92}));
        CHECK(out["weather"]["enabled"] == true);
        CHECK(out["weather"]["overrides"] == nlohmann::json::array({"uniformDensity", "fogColor", "heightFogDensity"}));
        // From the effective values: (0.5 + 0.1) * (0.1 + 0.15) = 0.15 per metre.
        CHECK(out["derived"]["extinctionAtGround"].get<double>() == doctest::Approx(0.15));
        CHECK(out["derived"]["visibilityMeters"].get<double>() == doctest::Approx(std::log(20.0) / 0.15));
        CHECK(contains(result.text, "weather overrides"));
    }

    SUBCASE("no fog density: below the raymarch cutoff, unlimited visibility")
    {
        engine.livePostProcess.volumetricFog.uniformDensity = 0.0f;
        engine.livePostProcess.volumetricFog.heightFogDensity = 0.0f;
        nlohmann::json out = fixture.callOk("fog_get", nlohmann::json::object());
        CHECK(out["derived"]["extinctionAtGround"] == 0.0);
        CHECK(out["derived"]["visibilityMeters"].is_null());
        CHECK(contains(out["derived"].value("note", std::string()), "cutoff"));
    }

    SUBCASE("disabled fog: no visibility")
    {
        engine.livePostProcess.volumetricFog.enabled = false;
        nlohmann::json out = fixture.callOk("fog_get", nlohmann::json::object());
        CHECK(out["derived"]["extinctionAtGround"].get<double>() == doctest::Approx(0.042));
        CHECK(out["derived"]["visibilityMeters"].is_null());
        CHECK(contains(out["derived"].value("note", std::string()), "disabled"));
    }

    SUBCASE("thin fog: the visibility reaches past maxDistance")
    {
        engine.livePostProcess.volumetricFog.uniformDensity = 0.001f;
        engine.livePostProcess.volumetricFog.heightFogDensity = 0.0f;
        // (0.5 + 0.1) * 0.001 = 0.0006 per metre: ln(20) / 0.0006 = 4993 m, beyond the 500 m the fog covers.
        nlohmann::json out = fixture.callOk("fog_get", nlohmann::json::object());
        CHECK(out["derived"]["visibilityMeters"].get<double>() == doctest::Approx(std::log(20.0) / 0.0006));
        CHECK(out["derived"]["limitedByMaxDistance"] == true);
        CHECK(contains(out["derived"].value("note", std::string()), "maxDistance"));
    }

    checkLog(engine.log, {});
}

TEST_CASE("mcp environment tools: gi_set applies the preset, then the overlay; debug views stay live-only")
{
    EnvironmentToolFixture fixture;
    FakeEnvironmentEngine& engine = fixture.engine;
    engine.liveGi.showProbes = true;
    engine.liveGi.showProbeValidity = true;
    engine.giStats.totalProbes = 800;
    engine.giStats.activeCascades = 3;

    SUBCASE("preset High with an overlay")
    {
        nlohmann::json out = fixture.callOk("gi_set", {
            {"preset", "high"}, {"settings", {{"ssgiIntensity", 1.25}, {"maxProbeDistance", 300}}}});

        env::GISettings expected = env::GISettings::fromQuality(::render::gi::GIQuality::High);
        expected.ssgiIntensity = 1.25f;
        expected.maxProbeDistance = 300.0f;
        const nlohmann::json expectedJson = env::toJson(env::giTable(), expected);
        CHECK(env::toJson(env::giTable(), engine.scene.gi) == expectedJson);
        CHECK_FALSE(engine.scene.gi.showProbes);
        CHECK_FALSE(engine.scene.gi.showCascadeBounds);
        CHECK_FALSE(engine.scene.gi.showProbeValidity);

        // The live copy runs the same settings but keeps this session's debug views.
        CHECK(env::toJson(env::giTable(), engine.liveGi) == expectedJson);
        CHECK(engine.liveGi.showProbes);
        CHECK_FALSE(engine.liveGi.showCascadeBounds);
        CHECK(engine.liveGi.showProbeValidity);

        CHECK(out["changed"] == true);
        CHECK(out["preset"] == "High");
        CHECK(out["rebuilt"] == true); // Off -> High builds the probes
        CHECK(out["settings"] == expectedJson);
        CHECK(out["diff"]["quality"] == "High");
        CHECK(out["diff"]["ssgiIntensity"] == 1.25);
        CHECK(out["stats"]["totalProbes"] == 800);
        CHECK(out["warnings"].empty());
        checkLog(engine.log, {"SetRenderSettings", "ApplyGISettings", "RenderSettingsChanged:GI", "PushUndo"});
        REQUIRE(engine.undoStack.size() == 1);
        CHECK(engine.undoStack[0]->getDescription() == "MCP: Set GI");

        engine.undoStack[0]->undo();
        CHECK(env::toJson(env::giTable(), engine.scene.gi) == env::toJson(env::giTable(), env::GISettings{}));
        CHECK(engine.liveGi.showProbes); // undo keeps the live debug views too
    }

    SUBCASE("the saved settings never carry debug views")
    {
        engine.scene.gi = env::GISettings::fromQuality(::render::gi::GIQuality::Medium);
        engine.scene.gi.showCascadeBounds = true; // e.g. saved by an older editor
        fixture.callOk("gi_set", {{"settings", {{"ssgiIntensity", 1.0}}}});
        CHECK_FALSE(engine.scene.gi.showCascadeBounds);
        CHECK(engine.scene.gi.quality == ::render::gi::GIQuality::Medium); // no preset: the saved settings are the base
        CHECK(engine.scene.gi.ssgiIntensity == 1.0f);
        CHECK_FALSE(engine.liveGi.showCascadeBounds);
        CHECK(engine.liveGi.showProbes);
    }

    SUBCASE("rebuilt follows the probe-grid fields")
    {
        engine.scene.gi = env::GISettings::fromQuality(::render::gi::GIQuality::High);
        engine.liveGi = engine.scene.gi;

        nlohmann::json tuning = fixture.callOk("gi_set", {{"settings", {{"ssgiIntensity", 0.75}}}});
        CHECK(tuning["rebuilt"] == false);
        nlohmann::json farField = fixture.callOk("gi_set", {{"settings", {{"farFieldProbeSpacing", 16}}}});
        CHECK(farField["rebuilt"] == true);
        nlohmann::json multiplier = fixture.callOk("gi_set", {{"settings", {{"cascadeMultiplier", 3}}}});
        CHECK(multiplier["rebuilt"] == true);
        nlohmann::json disabled = fixture.callOk("gi_set", {{"settings", {{"enabled", false}}}});
        CHECK(disabled["rebuilt"] == true);
        REQUIRE(disabled["warnings"].size() == 1);
        CHECK(contains(disabled["warnings"][0].get<std::string>(), "but disabled"));
        CHECK(engine.undoStack.size() == 4);
    }

    SUBCASE("probeSpacing below 1 m is rejected and changes nothing")
    {
        CHECK(contains(fixture.argError("gi_set", {{"settings", {{"probeSpacing", 0.5}}}}),
                       "'settings.probeSpacing' must be between 1.0 and 8.0 (got 0.5)"));
        checkLog(engine.log, {});
        CHECK(engine.undoStack.empty());
    }
}

TEST_CASE("mcp environment tools: gi_get reports saved, live, stats, debug views and presets")
{
    EnvironmentToolFixture fixture;
    FakeEnvironmentEngine& engine = fixture.engine;
    engine.scene.gi = env::GISettings::fromQuality(::render::gi::GIQuality::Medium);
    engine.liveGi = engine.scene.gi;
    engine.liveGi.ssgiIntensity = 0.9f;
    engine.liveGi.showProbes = true;
    engine.giStats.totalProbes = 256;
    engine.giStats.activeCascades = 1;
    engine.giStats.probesUpdatedThisFrame = 64;

    nlohmann::json out = fixture.callOk("gi_get", nlohmann::json::object());
    CHECK(out["authored"] == env::toJson(env::giTable(), engine.scene.gi));
    CHECK(out["live"]["ssgiIntensity"] == 0.9);
    CHECK_FALSE(out["live"].contains("showProbes"));
    CHECK(out["active"] == true);
    CHECK(out["stats"] == nlohmann::json{{"totalProbes", 256}, {"activeCascades", 1}, {"probesUpdatedThisFrame", 64}});
    CHECK(out["debug"] == nlohmann::json{{"probes", true}, {"cascadeBounds", false}, {"probeValidity", false}});
    CHECK(out["presets"] == nlohmann::json::array({"Off", "Medium", "High", "Ultra"}));
    CHECK(out["ranges"]["probeSpacing"] == nlohmann::json{{"type", "number"}, {"min", 1.0}, {"max", 8.0}});
    CHECK(out["ranges"]["ssgiSampleCount"] == nlohmann::json{{"type", "integer"}, {"min", 4}, {"max", 16}});
    CHECK(out["ranges"]["quality"]["values"] == nlohmann::json::array({"Off", "Medium", "High", "Ultra"}));
    CHECK_FALSE(out["ranges"].contains("showProbes"));
    REQUIRE(out["warnings"].size() == 1);
    CHECK(contains(out["warnings"][0].get<std::string>(), "ssgiIntensity"));
    checkLog(engine.log, {});
}

TEST_CASE("mcp environment tools: gi_debug toggles only the live debug views, without undo")
{
    EnvironmentToolFixture fixture;
    FakeEnvironmentEngine& engine = fixture.engine;
    const nlohmann::json sceneBefore = fixture.sceneJson();

    nlohmann::json out = fixture.callOk("gi_debug", {{"probes", true}, {"probeValidity", false}});
    checkLog(engine.log, {"SetGIDebugProbes", "SetGIDebugProbeValidity"});
    CHECK(engine.liveGi.showProbes);
    CHECK_FALSE(engine.liveGi.showProbeValidity);
    CHECK(out["debug"] == nlohmann::json{{"probes", true}, {"cascadeBounds", false}, {"probeValidity", false}});
    CHECK(out["active"] == false);
    CHECK_FALSE(out["warnings"].empty()); // GI is off: the views wait for it
    CHECK(contains(out["note"].get<std::string>(), "session-only"));

    CHECK(engine.undoStack.empty());
    CHECK(engine.notifications.empty());
    CHECK(fixture.sceneJson() == sceneBefore);
    CHECK_FALSE(engine.scene.gi.showProbes);
}

TEST_CASE("mcp environment tools: ibl_set hdr keeps paths inside the project and needs an existing .vfHdr")
{
    EnvironmentToolFixture fixture;
    FakeEnvironmentEngine& engine = fixture.engine;
    const fs::path& assets = engine.assets;
    touch(assets / "skies" / "sky.hdr");
    const fs::path outside = assets.parent_path() / "VertexForge_McpEnvironmentOutside.vfHdr";
    touch(outside);

    CHECK(contains(fixture.argError("ibl_set", {{"source", "hdr"}, {"path", "../VertexForge_McpEnvironmentOutside.vfHdr"}}),
                   "outside the allowed root"));
    CHECK(contains(fixture.argError("ibl_set", {{"source", "hdr"}, {"path", "skies/sky.hdr"}}), "must have the .vfHdr extension"));
    CHECK(contains(fixture.argError("ibl_set", {{"source", "hdr"}, {"path", "skies/missing.vfHdr"}}), "does not exist"));
    CHECK(contains(fixture.argError("ibl_set", {{"source", "hdr"}, {"path", ""}}), "must not be empty"));
    CHECK(contains(fixture.argError("ibl_set", {{"source", "hdr"}}), "argument 'path' is required"));

    checkLog(engine.log, {});
    CHECK(engine.undoStack.empty());
    CHECK_FALSE(engine.rootIbl.has_value());
    std::error_code ec;
    fs::remove(outside, ec);
}

TEST_CASE("mcp environment tools: ibl_set hdr bakes, then turns the procedural sky off; one undo step")
{
    EnvironmentToolFixture fixture;
    FakeEnvironmentEngine& engine = fixture.engine;
    touch(engine.assets / "skies" / "sky.vfHdr");
    engine.scene.atmosphere.enabled = true;
    engine.scene.atmosphere.dynamicAmbient = true;
    engine.liveAtmosphere = engine.scene.atmosphere;
    const nlohmann::json hdrArgs{
        {"source", "hdr"}, {"path", "skies/sky.vfHdr"}, {"intensity", 2.0}, {"rotationDeg", 90}, {"tint", {1.0, 0.5, 0.25}}};

    nlohmann::json out = fixture.callOk("ibl_set", hdrArgs);
    // The IBL first (component, bake, knobs), then the sky flags.
    checkLog(engine.log, {"SetIBLData", "SetIBL", "SetIBLParams", "RenderSettingsChanged:IBL",
                          "SetRenderSettings", "ApplyAtmosphereSettingsNoSun", "RenderSettingsChanged:Atmosphere",
                          "PushUndo"});
    REQUIRE(engine.bakedHdrPaths.size() == 1);
    std::error_code ec;
    CHECK(fs::equivalent(fs::path(engine.bakedHdrPaths[0]), engine.assets / "skies" / "sky.vfHdr", ec));
    REQUIRE(engine.rootIbl.has_value());
    CHECK(engine.rootIbl->hdrRef.isValid());
    CHECK(engine.rootIbl->intensity == 2.0f);
    CHECK(engine.rootIbl->rotationDeg == 90.0f);
    CHECK(engine.rootIbl->tint == glm::vec3(1.0f, 0.5f, 0.25f));
    REQUIRE(engine.iblParams.size() == 1);
    CHECK(engine.iblParams[0].intensity == 2.0f);
    CHECK(engine.iblParams[0].rotationDeg == 90.0f);
    CHECK_FALSE(engine.scene.atmosphere.enabled);
    CHECK_FALSE(engine.scene.atmosphere.dynamicAmbient);
    CHECK_FALSE(engine.liveAtmosphere.enabled);

    CHECK(out["changed"] == true);
    CHECK(out["rebaked"] == true);
    CHECK(out["source"] == "hdr");
    CHECK(out["ibl"]["path"] == "skies/sky.vfHdr");
    CHECK(out["ibl"]["intensity"] == 2.0);
    CHECK(out["ibl"]["tint"] == nlohmann::json::array({1.0, 0.5, 0.25}));
    CHECK(out["sky"] == "hdr");
    CHECK(out["ambient"] == "hdr");
    CHECK(out["atmosphere"] == nlohmann::json{{"enabled", false}, {"dynamicAmbient", false}});
    REQUIRE(engine.undoStack.size() == 1);
    CHECK(engine.undoStack[0]->getDescription() == "MCP: Set IBL (hdr)");
    CHECK(batchSize(engine.undoStack[0]) == 2);

    SUBCASE("undo without an earlier IBL removes it and restores the sky; redo re-bakes")
    {
        engine.log.clear();
        engine.undoStack[0]->undo();
        checkLog(engine.log, {"SetRenderSettings", "ApplyAtmosphereSettingsNoSun", "RenderSettingsChanged:Atmosphere",
                              "RemoveIBL", "RemoveIBLComponent", "RenderSettingsChanged:IBL"});
        CHECK_FALSE(engine.rootIbl.has_value());
        CHECK_FALSE(engine.rendererHdr.has_value());
        CHECK(engine.scene.atmosphere.enabled);
        CHECK(engine.scene.atmosphere.dynamicAmbient);
        CHECK(engine.liveAtmosphere.dynamicAmbient);

        engine.log.clear();
        engine.undoStack[0]->execute();
        checkLog(engine.log, {"SetIBLData", "SetIBL", "SetIBLParams", "RenderSettingsChanged:IBL",
                              "SetRenderSettings", "ApplyAtmosphereSettingsNoSun", "RenderSettingsChanged:Atmosphere"});
        REQUIRE(engine.rootIbl.has_value());
        CHECK(engine.rootIbl->intensity == 2.0f);
        CHECK_FALSE(engine.scene.atmosphere.enabled);
    }

    SUBCASE("a knob-only change keeps the bake")
    {
        engine.log.clear();
        nlohmann::json knobs = fixture.callOk("ibl_set", {{"source", "hdr"}, {"intensity", 3.0}});
        checkLog(engine.log, {"SetIBLData", "SetIBLParams", "RenderSettingsChanged:IBL", "PushUndo"});
        CHECK(knobs["rebaked"] == false);
        REQUIRE(engine.rootIbl.has_value());
        CHECK(engine.rootIbl->intensity == 3.0f);
        CHECK(engine.rootIbl->rotationDeg == 90.0f); // the knobs not passed carry over
        CHECK(engine.rootIbl->tint == glm::vec3(1.0f, 0.5f, 0.25f));
        REQUIRE(engine.undoStack.size() == 2);

        // Undo re-pushes the old knobs only: the HDR did not change.
        engine.log.clear();
        engine.undoStack[1]->undo();
        checkLog(engine.log, {"SetIBLData", "SetIBLParams", "RenderSettingsChanged:IBL"});
        CHECK(engine.rootIbl->intensity == 2.0f);
    }

    SUBCASE("the same request again changes nothing")
    {
        engine.log.clear();
        nlohmann::json again = fixture.callOk("ibl_set", hdrArgs);
        CHECK(again["changed"] == false);
        CHECK(again["rebaked"] == false);
        checkLog(engine.log, {});
        CHECK(engine.undoStack.size() == 1);
    }
}

TEST_CASE("mcp environment tools: ibl_set hdr with showAsSky:false under the dynamic sky only stages the HDR")
{
    EnvironmentToolFixture fixture;
    FakeEnvironmentEngine& engine = fixture.engine;
    touch(engine.assets / "skies" / "sky.vfHdr");
    engine.scene.atmosphere.enabled = true;
    engine.scene.atmosphere.dynamicAmbient = true;
    engine.liveAtmosphere = engine.scene.atmosphere;

    nlohmann::json out = fixture.callOk("ibl_set", {{"source", "hdr"}, {"path", "skies/sky.vfHdr"}, {"showAsSky", false}});
    // The renderer receives the HDR, but the sky capture keeps the ambient: nothing is baked yet, and the
    // atmosphere flags are left as they are.
    checkLog(engine.log, {"SetIBLData", "SetIBL", "SetIBLParams", "RenderSettingsChanged:IBL", "PushUndo"});
    CHECK(out["changed"] == true);
    CHECK(out["rebaked"] == false);
    CHECK(out["atmosphere"].empty());
    CHECK(out["sky"] == "atmosphere");
    CHECK(out["ambient"] == "dynamicSky");
    REQUIRE(out["warnings"].size() == 1);
    CHECK(contains(out["warnings"][0].get<std::string>(), "neither visible nor used for lighting"));
    CHECK(engine.scene.atmosphere.enabled);
    CHECK(engine.scene.atmosphere.dynamicAmbient);
    REQUIRE(engine.undoStack.size() == 1);
    CHECK(batchSize(engine.undoStack[0]) == 1);
}

TEST_CASE("mcp environment tools: an HDR the renderer cannot load rolls ibl_set back")
{
    EnvironmentToolFixture fixture;
    FakeEnvironmentEngine& engine = fixture.engine;
    touch(engine.assets / "skies" / "sky.vfHdr");
    engine.hdrLoads = false;

    const mcp::ToolResult result = fixture.call("ibl_set", {{"source", "hdr"}, {"path", "skies/sky.vfHdr"}});
    CHECK(result.isError);
    CHECK(contains(result.text, "rolled back"));
    CHECK(contains(result.text, "could not load the HDR environment"));
    CHECK_FALSE(engine.rootIbl.has_value());
    CHECK(engine.undoStack.empty());
    checkLog(engine.log, {"SetIBLData", "SetIBL", "RemoveIBL", "RemoveIBLComponent", "RenderSettingsChanged:IBL"});
}

TEST_CASE("mcp environment tools: ibl_set dynamicSky and none")
{
    EnvironmentToolFixture fixture;
    FakeEnvironmentEngine& engine = fixture.engine;
    touch(engine.assets / "skies" / "sky.vfHdr");
    services::IBLData hdr;
    hdr.hdrRef = asset::AssetRef::fromPath((engine.assets / "skies" / "sky.vfHdr").string());
    REQUIRE(hdr.hdrRef.isValid());
    hdr.intensity = 1.5f;
    engine.rootIbl = hdr;

    SUBCASE("dynamicSky: the atmosphere is the sky and lights the scene; a Sun is created")
    {
        nlohmann::json out = fixture.callOk("ibl_set", {{"source", "dynamicSky"}, {"ambientIntensity", 2.0}});
        CHECK(engine.scene.atmosphere.enabled);
        CHECK(engine.scene.atmosphere.dynamicAmbient);
        CHECK(engine.scene.atmosphere.ambientIntensity == 2.0f);
        CHECK(engine.liveAtmosphere.dynamicAmbient);
        CHECK(engine.findByName("Sun").has_value());
        CHECK(engine.rootIbl.has_value()); // the HDR stays on the root; the sky capture owns the ambient
        CHECK(out["sky"] == "atmosphere");
        CHECK(out["ambient"] == "dynamicSky");
        CHECK(out["sun"]["created"] == true);
        CHECK(out["atmosphere"] == nlohmann::json{{"enabled", true}, {"dynamicAmbient", true}, {"ambientIntensity", 2.0}});
        REQUIRE(engine.undoStack.size() == 1);
        CHECK(engine.undoStack[0]->getDescription() == "MCP: Set IBL (dynamic sky)");
        CHECK(indexOf(engine.log, "ApplyAtmosphereSettings") == -1);
    }

    SUBCASE("none: the HDR is removed; undo re-bakes it")
    {
        nlohmann::json out = fixture.callOk("ibl_set", {{"source", "none"}});
        checkLog(engine.log, {"RemoveIBL", "RemoveIBLComponent", "RenderSettingsChanged:IBL", "PushUndo"});
        CHECK_FALSE(engine.rootIbl.has_value());
        CHECK(out["changed"] == true);
        CHECK(out["ibl"].is_null());
        CHECK(out["sky"] == "none");
        CHECK(out["ambient"] == "default");

        engine.log.clear();
        engine.undoStack[0]->undo();
        checkLog(engine.log, {"SetIBLData", "SetIBL", "SetIBLParams", "RenderSettingsChanged:IBL"});
        REQUIRE(engine.rootIbl.has_value());
        CHECK(engine.rootIbl->intensity == 1.5f);
        CHECK(engine.rootIbl->hdrRef == hdr.hdrRef);
    }

    SUBCASE("none without an IBL changes nothing")
    {
        engine.rootIbl.reset();
        nlohmann::json out = fixture.callOk("ibl_set", {{"source", "none"}});
        CHECK(out["changed"] == false);
        checkLog(engine.log, {});
        CHECK(engine.undoStack.empty());
    }
}

TEST_CASE("mcp environment tools: ibl_bake bakes an HDR, leaves the dynamic sky to itself and queues probes")
{
    EnvironmentToolFixture fixture;
    FakeEnvironmentEngine& engine = fixture.engine;

    SUBCASE("dynamic sky: captured automatically")
    {
        engine.liveAtmosphere.enabled = true;
        engine.liveAtmosphere.dynamicAmbient = true;
        nlohmann::json out = fixture.callOk("ibl_bake", {{"target", "environment"}});
        CHECK(out["environment"]["mode"] == "dynamicSky");
        CHECK(out["environment"]["baked"] == false);
        CHECK(contains(out["environment"]["note"].get<std::string>(), "automatically"));
        CHECK_FALSE(out.contains("probes"));
        checkLog(engine.log, {});
    }

    SUBCASE("HDR: re-baked now")
    {
        touch(engine.assets / "skies" / "sky.vfHdr");
        services::IBLData hdr;
        hdr.hdrRef = asset::AssetRef::fromPath((engine.assets / "skies" / "sky.vfHdr").string());
        REQUIRE(hdr.hdrRef.isValid());
        engine.rootIbl = hdr;
        nlohmann::json out = fixture.callOk("ibl_bake", {{"target", "environment"}});
        checkLog(engine.log, {"SetIBL"});
        CHECK(out["environment"]["mode"] == "hdr");
        CHECK(out["environment"]["baked"] == true);
        CHECK(out["environment"]["path"] == "skies/sky.vfHdr");
    }

    SUBCASE("nothing to bake")
    {
        nlohmann::json out = fixture.callOk("ibl_bake", {{"target", "environment"}});
        CHECK(out["environment"]["mode"] == "none");
        CHECK(out["environment"]["baked"] == false);
        checkLog(engine.log, {});
    }

    SUBCASE("probes: every probe, or the one entity")
    {
        nlohmann::json all = fixture.callOk("ibl_bake", {{"target", "probes"}});
        CHECK(all["probes"]["queued"] == true);
        CHECK(all["probes"]["async"] == true);
        CHECK(all["probes"]["entity"].is_null());
        CHECK_FALSE(all.contains("environment"));
        REQUIRE(engine.probeBakes.size() == 1);
        CHECK_FALSE(engine.probeBakes[0].isValid()); // invalid handle = every probe

        nlohmann::json one = fixture.callOk("ibl_bake", {{"entity", 42}}); // target defaults to all
        CHECK(one["probes"]["entity"] == 42);
        CHECK(one.contains("environment"));
        REQUIRE(engine.probeBakes.size() == 2);
        CHECK(engine.probeBakes[1].id == 42);
    }

    SUBCASE("no reflection probes: a note, or an error for a named entity")
    {
        engine.probesQueued = false;
        nlohmann::json out = fixture.callOk("ibl_bake", {{"target", "probes"}});
        CHECK(out["probes"]["queued"] == false);
        CHECK(contains(out["probes"]["note"].get<std::string>(), "no reflection probes"));

        const mcp::ToolResult named = fixture.call("ibl_bake", {{"target", "probes"}, {"entity", 42}});
        CHECK(named.isError);
        CHECK(contains(named.text, "42 is not a reflection probe"));
    }

    CHECK(engine.undoStack.empty());
    CHECK(engine.notifications.empty());
}

TEST_CASE("mcp environment tools: weather_set Fog turns weather on in order and is one undo step")
{
    EnvironmentToolFixture fixture;
    FakeEnvironmentEngine& engine = fixture.engine;
    const env::FogSettings fogBefore = engine.scene.postProcess.volumetricFog;
    const ::weather::WeatherState& fogPreset = ::weather::getPreset(::weather::WeatherPresetId::Fog);

    nlohmann::json out = fixture.callOk("weather_set", {{"preset", "Fog"}, {"enableVolumetricFog", true}});
    checkLog(engine.log, {"SetRenderSettings", "ApplyPostProcessSettings", "RenderSettingsChanged:VolumetricFog",
                          "SetWeatherEnabled", "SetWeatherImmediate", "UpdateWeather", "PushUndo"});
    REQUIRE(engine.weatherUpdates.size() == 1);
    CHECK(engine.weatherUpdates[0] == 0.0f);
    CHECK(engine.weatherEnabled);
    CHECK(env::sameWeatherState(engine.weatherState, fogPreset, 0.0f));

    // Volumetric fog is switched on in the scene; weather drives the live density and colour.
    CHECK(engine.scene.postProcess.volumetricFog.enabled);
    CHECK(engine.scene.postProcess.volumetricFog.uniformDensity == fogBefore.uniformDensity);
    const env::FogSettings& live = engine.livePostProcess.volumetricFog;
    CHECK(live.enabled);
    CHECK(live.uniformDensity == 0.1f);
    CHECK(live.heightFogDensity == 0.15f);
    CHECK(live.fogColor[0] == 0.88f);
    CHECK(live.fogColor[1] == 0.9f);
    CHECK(live.fogColor[2] == 0.92f);

    CHECK(out["changed"] == true);
    CHECK(out["enabled"] == true);
    CHECK(out["preset"] == "Fog");
    CHECK(out["transitioning"] == false);
    CHECK(out["state"]["fogDensity"] == 0.1);
    CHECK(out["effectiveFog"]["enabled"] == true);
    CHECK(out["effectiveFog"]["uniformDensity"] == 0.1);
    CHECK(out["effectiveFog"]["heightFogDensity"] == 0.15);
    CHECK(out["effectiveFog"]["fogColor"] == nlohmann::json::array({0.88, 0.9, 0.92}));
    CHECK(contains(out["note"].get<std::string>(), "session-only"));
    CHECK(out["warnings"].empty());
    REQUIRE(engine.undoStack.size() == 1);
    CHECK(engine.undoStack[0]->getDescription() == "MCP: Set weather (Fog)");
    CHECK(batchSize(engine.undoStack[0]) == 2);

    // Undo: weather off with its state reset, then the scene's fog as it was (volumetric fog off again).
    engine.log.clear();
    engine.undoStack[0]->undo();
    CHECK_FALSE(engine.weatherEnabled);
    CHECK(env::sameWeatherState(engine.weatherState, ::weather::WeatherState{}, 0.0f));
    CHECK(env::toJson(env::fogTable(), engine.scene.postProcess.volumetricFog) == env::toJson(env::fogTable(), fogBefore));
    CHECK(env::toJson(env::fogTable(), engine.livePostProcess.volumetricFog) == env::toJson(env::fogTable(), fogBefore));
    checkLog(engine.log, {"SetWeatherEnabled", "SetWeatherImmediate",
                          "SetRenderSettings", "ApplyPostProcessSettings", "RenderSettingsChanged:VolumetricFog"});

    // Redo: the fog section, then the weather in the order that survives the reset.
    engine.log.clear();
    engine.undoStack[0]->execute();
    CHECK(engine.weatherEnabled);
    CHECK(env::sameWeatherState(engine.weatherState, fogPreset, 0.0f));
    CHECK(engine.scene.postProcess.volumetricFog.enabled);
    CHECK(engine.livePostProcess.volumetricFog.uniformDensity == 0.1f);
    checkLog(engine.log, {"SetRenderSettings", "ApplyPostProcessSettings", "RenderSettingsChanged:VolumetricFog",
                          "SetWeatherEnabled", "SetWeatherImmediate", "UpdateWeather"});
}

TEST_CASE("mcp environment tools: weather_set transitions, turns weather off and refuses bad presets")
{
    EnvironmentToolFixture fixture;
    FakeEnvironmentEngine& engine = fixture.engine;

    SUBCASE("transitionSeconds > 0 blends with SetWeatherPreset")
    {
        nlohmann::json out = fixture.callOk("weather_set", {{"preset", "overcast"}, {"transitionSeconds", 5}});
        CHECK(indexOf(engine.log, "SetWeatherImmediate") == -1);
        REQUIRE(engine.presetTransitions.size() == 1);
        CHECK(engine.presetTransitions[0].preset == ::weather::WeatherPresetId::Overcast);
        CHECK(engine.presetTransitions[0].transitionDuration == 5.0f);
        CHECK(indexOf(engine.log, "SetWeatherEnabled") < indexOf(engine.log, "SetWeatherPreset"));
        CHECK(indexOf(engine.log, "SetWeatherPreset") < indexOf(engine.log, "UpdateWeather"));
        CHECK(out["preset"] == "Overcast");
        CHECK(out["transitioning"] == true);
        REQUIRE(engine.undoStack.size() == 1);
        CHECK(engine.undoStack[0]->getDescription() == "MCP: Set weather (Overcast)");
    }

    SUBCASE("enabled:false restores the scene's fog; undo brings the weather back")
    {
        engine.scene.postProcess.volumetricFog.enabled = true;
        engine.livePostProcess.volumetricFog = engine.scene.postProcess.volumetricFog;
        engine.weatherEnabled = true;
        engine.weatherState = ::weather::getPreset(::weather::WeatherPresetId::HeavyRain);
        engine.applyFogWeather();

        nlohmann::json out = fixture.callOk("weather_set", {{"enabled", false}});
        checkLog(engine.log, {"SetWeatherEnabled", "PushUndo"});
        CHECK_FALSE(engine.weatherEnabled);
        CHECK(engine.livePostProcess.volumetricFog.uniformDensity == engine.scene.postProcess.volumetricFog.uniformDensity);
        CHECK(out["enabled"] == false);
        CHECK(out["preset"].is_null());

        engine.undoStack[0]->undo();
        CHECK(engine.weatherEnabled);
        CHECK(env::sameWeatherState(engine.weatherState, ::weather::getPreset(::weather::WeatherPresetId::HeavyRain), 0.0f));
        CHECK(engine.livePostProcess.volumetricFog.uniformDensity == 0.03f); // HeavyRain fogDensity
    }

    SUBCASE("unknown presets list the valid names; enabled:false cannot take a preset")
    {
        const std::string unknown = fixture.argError("weather_set", {{"preset", "Tornado"}});
        CHECK(contains(unknown, "Clear, Cloudy, Overcast, LightRain, HeavyRain, Thunderstorm, LightSnow, HeavySnow, Fog, Sandstorm"));
        CHECK(contains(unknown, "'Tornado'"));
        CHECK(contains(fixture.argError("weather_set", {{"preset", "Fog"}, {"enabled", false}}),
                       "a preset turns weather on"));
        checkLog(engine.log, {});
        CHECK(engine.undoStack.empty());
    }

    CHECK(engine.batchCommands == 0);
}

TEST_CASE("mcp environment tools: weather_get reports the session weather and the preset it matches")
{
    EnvironmentToolFixture fixture;
    FakeEnvironmentEngine& engine = fixture.engine;
    engine.weatherEnabled = true;
    engine.weatherState = ::weather::getPreset(::weather::WeatherPresetId::Fog);
    engine.applyFogWeather(); // the live volumetric fog itself stays disabled

    nlohmann::json out = fixture.callOk("weather_get", nlohmann::json::object());
    CHECK(out["enabled"] == true);
    CHECK(out["matchesPreset"] == "Fog");
    CHECK(out["state"]["fogDensity"] == 0.1);
    CHECK(out["state"]["precipType"] == "None");
    CHECK(out["transitioning"] == false);
    CHECK(out["progress"] == 1.0);
    REQUIRE(out["presets"].size() == 10);
    CHECK(out["presets"][8] == "Fog");
    CHECK(out["effectiveFog"]["enabled"] == false);
    CHECK(out["effectiveFog"]["uniformDensity"] == 0.1);
    CHECK(out["scheduleEnabled"] == false);
    CHECK(contains(out["note"].get<std::string>(), "session-only"));
    REQUIRE(out["warnings"].size() == 1);
    CHECK(contains(out["warnings"][0].get<std::string>(), "invisible"));

    engine.weatherState.windSpeed = 7.0f;
    nlohmann::json custom = fixture.callOk("weather_get", nlohmann::json::object());
    CHECK(custom["matchesPreset"].is_null());
    checkLog(engine.log, {});
}

TEST_CASE("mcp environment tools: environment_get reports saved settings, live overrides, sun, sky and ranges")
{
    EnvironmentToolFixture fixture;
    FakeEnvironmentEngine& engine = fixture.engine;
    env::AtmosphereSettings& atmosphere = engine.scene.atmosphere;
    atmosphere.enabled = true;
    atmosphere.sunAzimuth = 135.0f;
    atmosphere.sunElevation = 30.0f;
    engine.liveAtmosphere = atmosphere;

    SUBCASE("with the sun light in sync and weather on")
    {
        engine.liveAtmosphere.aerialIntensity = 0.5f; // as weather drives it
        engine.addLight(3, "Sun", true, directionalLightEulerForSun(135.0f, 30.0f));
        engine.nodes.at(3).light->intensity = 4.0f;
        engine.weatherEnabled = true;

        nlohmann::json out = fixture.callOk("environment_get", nlohmann::json::object());
        CHECK(out["atmosphere"] == env::toJson(env::atmosphereTable(), atmosphere));
        CHECK(out["clouds"] == env::toJson(env::cloudTable(), engine.scene.cloud));
        CHECK(out["liveOverrides"]["atmosphere"] == nlohmann::json{{"aerialIntensity", 0.5}});
        CHECK(out["liveOverrides"]["clouds"].empty());
        CHECK(out["timeOfDay"] == 12.0);
        CHECK(out["dayNightEnabled"] == false);
        CHECK(out["sun"]["entity"] == 3);
        CHECK(out["sun"]["name"] == "Sun");
        CHECK(out["sun"]["azimuth"].get<double>() == doctest::Approx(135.0).epsilon(1e-4));
        CHECK(out["sun"]["elevation"].get<double>() == doctest::Approx(30.0).epsilon(1e-4));
        CHECK(out["sun"]["color"] == nlohmann::json::array({1.0, 1.0, 1.0}));
        CHECK(out["sun"]["intensity"] == 4.0);
        CHECK(out["sun"]["inSync"] == true);
        CHECK(out["sky"] == "atmosphere");
        CHECK(out["ambient"] == "default");
        CHECK(out["ibl"].is_null());
        CHECK(out["weather"]["enabled"] == true);
        CHECK(out["weather"]["drivenFields"] == nlohmann::json::array({
            "atmosphere.sunIrradiance", "atmosphere.aerialIntensity", "atmosphere.ambientIntensity",
            "clouds.globalDensity", "clouds.globalCoverage", "clouds.cloudType", "clouds.windSpeed",
            "clouds.windDirectionDeg", "clouds.cloudColorTint", "clouds.ambientIntensity"}));
        CHECK(out["ranges"]["atmosphere"]["mieAnisotropy"] == nlohmann::json{{"type", "number"}, {"min", -1.0}, {"max", 1.0}});
        CHECK(out["ranges"]["atmosphere"]["moonAzimuth"]["max"] == 359.99997); // the float bound, shortest form
        CHECK(out["ranges"]["atmosphere"]["timeOfDay"]["readOnly"] == "set it with the timeOfDay argument");
        CHECK(out["ranges"]["atmosphere"]["sunIrradiance"]["weatherDriven"] == true);
        CHECK(out["ranges"]["clouds"]["maxMarchSteps"] == nlohmann::json{{"type", "integer"}, {"min", 16}, {"max", 256}});
        CHECK(out["warnings"].empty());

        nlohmann::json withoutRanges = fixture.callOk("environment_get", {{"includeRanges", false}});
        CHECK_FALSE(withoutRanges.contains("ranges"));
    }

    SUBCASE("without a directional light")
    {
        nlohmann::json out = fixture.callOk("environment_get", nlohmann::json::object());
        CHECK(out["sun"].is_null());
        CHECK(out["weather"]["drivenFields"].empty());
        REQUIRE(out["warnings"].size() == 1);
        CHECK(contains(out["warnings"][0].get<std::string>(), "no directional light"));
    }

    checkLog(engine.log, {});
}

TEST_CASE("mcp environment tools: edits that change nothing report changed:false and push no undo")
{
    EnvironmentToolFixture fixture;
    FakeEnvironmentEngine& engine = fixture.engine;
    engine.addLight(3, "Sun", true, glm::vec3(-45.0f, 0.0f, 0.0f));

    nlohmann::json environmentResult = fixture.callOk("environment_set", {
        {"atmosphere", {{"mieAnisotropy", 0.8}}}, {"clouds", {{"globalCoverage", 0.45}}}});
    CHECK(environmentResult["changed"] == false);
    CHECK(contains(environmentResult["note"].get<std::string>(), "no undo step"));
    nlohmann::json fogResult = fixture.callOk("fog_set", {{"uniformDensity", 0.02}});
    CHECK(fogResult["changed"] == false);
    nlohmann::json giResult = fixture.callOk("gi_set", {{"settings", {{"probeSpacing", 2}}}});
    CHECK(giResult["changed"] == false);
    CHECK(giResult["rebuilt"] == false);
    nlohmann::json weatherResult = fixture.callOk("weather_set", {{"enabled", false}});
    CHECK(weatherResult["changed"] == false);
    nlohmann::json iblResult = fixture.callOk("ibl_set", {{"source", "none"}});
    CHECK(iblResult["changed"] == false);

    checkLog(engine.log, {});
    CHECK(engine.undoStack.empty());
    CHECK(engine.notifications.empty());
}
