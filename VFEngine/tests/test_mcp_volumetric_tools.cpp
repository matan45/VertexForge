#include <doctest.h>
#include <ostream> // doctest stringifies std::string_view and nlohmann::json through operator<<

#include "dispatch/MainThreadQueue.hpp"
#include "protocol/ArgReader.hpp"
#include "protocol/ToolRegistry.hpp"
#include "tools/CoreTools.hpp"
#include "undo/EntityIdRemap.hpp"

#include "navigation/volumetric/VolumetricPathfinder.hpp"
#include "navigation/volumetric/VolumetricTypes.hpp"
#include "navigation/volumetric/VolumetricVoxelizer.hpp"

#include "data/DTOs.hpp"
#include "data/UndoTypes.hpp"
#include "data/VolumetricNavData.hpp"
#include "events/EventDispatcher.hpp"
#include "events/editor/EditorModeEvents.hpp"
#include "events/editor/UndoRedoEvents.hpp"
#include "events/project/ProjectEvents.hpp"
#include "events/render/DebugDrawEvents.hpp"
#include "events/scene/ComponentPhysicsLightEvents.hpp"
#include "events/scene/EntityTransformEvents.hpp"
#include "events/scene/SceneSettingsEvents.hpp"
#include "events/volumetric/VolumetricNavEvents.hpp"
#include "events/world/WorldSectorEvents.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <future>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// VK-1660: the MCP volumetric tools (volumetric_bake, volumetric_get_info, volumetric_find_path, volumetric_clear) and
// the VolumetricNavVolume / VolumetricAgent component bindings, against a fake engine. The fake answers the
// events::volumetric events the way VolumetricNavServiceImpl does: a bake is a ticket whose outcome reads Running
// for a few polls and then Succeeded (10 x 10 x 10 voxels, 750 navigable) or the result a case sets; the Save
// command writes into the project's assets/volnav/Scene folder; FindPath3D returns the result a case sets.
// volumetric_bake is a Worker tool: the cases play the editor main thread by draining the queue meanwhile.

namespace
{
    namespace fs = std::filesystem;
    using json = nlohmann::json;

    constexpr uint64_t kRootId = 1;
    constexpr uint64_t kVolumeId = 10;
    constexpr uint64_t kSecondVolumeId = 11;
    constexpr uint64_t kDroneId = 20;

    constexpr int kBakedEdge = 10;
    constexpr int64_t kBakedVoxels = 1000;
    constexpr int64_t kBakedNavigable = 750;
    constexpr double kBakeMs = 12.4;

    services::EntityHandle handleOf(uint64_t id)
    {
        services::EntityHandle handle;
        handle.id = id;
        return handle;
    }

    struct DispatcherScope
    {
        DispatcherScope() { clearAll(); }
        ~DispatcherScope() { clearAll(); }

        static void clearAll()
        {
            events::EventDispatcher::instance().clear();
            mcp::undo::EntityIdRemap::instance().clear();
        }
    };

    bool textHas(const std::string& text, const std::string& needle)
    {
        return text.find(needle) != std::string::npos;
    }

    // Some entry of a JSON list of strings contains `needle`.
    bool listHas(const json& list, const std::string& needle)
    {
        if (!list.is_array())
        {
            return false;
        }
        return std::any_of(list.begin(), list.end(), [&needle](const json& entry)
        {
            return entry.is_string() && textHas(entry.get<std::string>(), needle);
        });
    }

    // The project the fake reports; never created (reported files are only made relative to it).
    fs::path projectRoot()
    {
        return fs::temp_directory_path() / "vf_mcp_volumetric_tools_project";
    }

    // ------------------------------------------------------------------
    // The fake engine
    // ------------------------------------------------------------------

    struct FakeVolume
    {
        services::VolumetricNavVolumeData data; // the writable fields and the read-only bake state
        uint64_t uuid = 0;
        services::VolumetricBakeState bakeState = services::VolumetricBakeState::Unknown;
        float progress = 0.0f;
    };

    struct FakeAgent
    {
        services::VolumetricAgentData data;
        services::VolumetricAgentState state;
    };

    struct FakeEntity
    {
        std::string name;
        glm::vec3 position{0.0f}; // world = local: every fake entity sits at the root
        std::optional<FakeVolume> volume;
        std::optional<FakeAgent> agent;
        bool navmeshAgent = false;
    };

    struct FakeBake
    {
        uint64_t ticket = 0;
        uint64_t entity = 0;
        int runningPolls = 0; // outcome polls still answered Running
        services::VolumetricBakeState result = services::VolumetricBakeState::Succeeded;
        std::string message;
        bool cancelled = false;
    };

    // VolumetricNavServiceImpl::setVolumeData's rules.
    bool volumeAccepted(const services::VolumetricNavVolumeData& data)
    {
        const auto finite = [](const glm::vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); };
        return finite(data.centerOffset) && finite(data.halfExtents) && std::isfinite(data.voxelSize) &&
               std::isfinite(data.agentClearance) && data.halfExtents.x > 0.0f && data.halfExtents.y > 0.0f &&
               data.halfExtents.z > 0.0f && data.voxelSize > 0.0f && (data.connectivity == 6 || data.connectivity == 26) &&
               data.agentClearance >= 0.0f;
    }

    // VolumetricNavServiceImpl::setAgentData's rules.
    bool agentAccepted(const services::VolumetricAgentData& data)
    {
        const float values[] = {data.radius, data.maxSpeed, data.maxAcceleration, data.stoppingDistance, data.turnSpeed};
        for (float value : values)
        {
            if (!std::isfinite(value))
            {
                return false;
            }
        }
        return data.radius > 0.0f && data.maxSpeed >= 0.0f && data.maxAcceleration >= 0.0f &&
               data.stoppingDistance >= 0.0f && data.turnSpeed >= 0.0f;
    }

    struct FakeVolumetricEngine
    {
        std::map<uint64_t, FakeEntity> entities;
        bool playMode = false;
        bool worldMode = false;
        bool loadPending = false;
        bool debugDrawEnabled = true;

        // Bakes
        int runningPolls = 1; // for every new bake
        int64_t navigable = kBakedNavigable;
        std::map<uint64_t, std::pair<services::VolumetricBakeState, std::string>> bakeResults; // per volume
        std::map<uint64_t, std::string> refusals;                                              // per volume
        std::map<uint64_t, std::string> saveErrors;                                            // per volume
        std::vector<FakeBake> bakes;
        uint64_t nextTicket = 100;
        std::vector<uint64_t> requested;
        std::vector<uint64_t> saved;
        std::vector<std::string> saveDirectories;
        std::vector<uint64_t> cancelled;
        std::vector<uint64_t> clears;
        int outcomePolls = 0;

        // Paths
        ::volumetric::VolumePathResult path;
        int pathQueries = 0;
        glm::vec3 pathStart{0.0f};
        glm::vec3 pathEnd{0.0f};
        float pathSnapRadius = -1.0f;
        std::vector<glm::vec3> navigablePoints; // IsPointNavigable3D answers true exactly for these
        std::vector<std::pair<glm::vec3, glm::vec3>> lines; // of the shown paths
        int pathShows = 0;
        float shownSeconds = 0.0f;

        int volumeWrites = 0;
        int agentWrites = 0;
        std::vector<std::shared_ptr<services::IUndoableCommand>> undoStack;

        FakeEntity& addEntity(uint64_t id, const std::string& name, const glm::vec3& position = glm::vec3(0.0f))
        {
            FakeEntity& entity = entities[id];
            entity.name = name;
            entity.position = position;
            return entity;
        }

        FakeVolume& addVolume(uint64_t id, const std::string& name, const glm::vec3& position,
                              const glm::vec3& halfExtents)
        {
            FakeEntity& entity = addEntity(id, name, position);
            entity.volume = FakeVolume{};
            entity.volume->uuid = 0xA000 + id;
            entity.volume->data.halfExtents = halfExtents;
            return *entity.volume;
        }

        // A volume whose grid is loaded from its saved file, as after a scene load.
        FakeVolume& addBakedVolume(uint64_t id, const std::string& name, const glm::vec3& position,
                                   const glm::vec3& halfExtents)
        {
            FakeVolume& volume = addVolume(id, name, position, halfExtents);
            volume.data.baked = true;
            volume.data.dims = glm::ivec3(kBakedEdge);
            volume.data.voxels = kBakedVoxels;
            volume.data.navigablePercent = 75.0f;
            volume.data.bakedFile = savedPath(name);
            return volume;
        }

        FakeAgent& addAgent(uint64_t id, const std::string& name, const glm::vec3& position, float radius = 0.5f)
        {
            FakeEntity& entity = addEntity(id, name, position);
            entity.agent = FakeAgent{};
            entity.agent->data.radius = radius;
            return *entity.agent;
        }

        static std::string savedPath(const std::string& name)
        {
            return (projectRoot() / "assets" / "volnav" / "Scene" / (name + ".vfVolNav")).string();
        }

        services::EntityData entityData(uint64_t id, const FakeEntity& entity) const
        {
            services::EntityData data;
            data.handle = handleOf(id);
            data.name = entity.name;
            data.localTransform.position = entity.position;
            data.worldTransform.position = entity.position;
            if (id != kRootId)
            {
                data.parent = handleOf(kRootId);
            }
            if (entity.volume.has_value())
            {
                data.components.push_back(services::ComponentTypeId::VolumetricNavVolume);
            }
            if (entity.agent.has_value())
            {
                data.components.push_back(services::ComponentTypeId::VolumetricAgent);
            }
            if (entity.navmeshAgent)
            {
                data.components.push_back(services::ComponentTypeId::NavmeshAgent);
            }
            return data;
        }

        // VolumetricNavServiceImpl::makeStatus: the box around the entity's world position.
        services::VolumetricVolumeStatus statusOf(uint64_t id, const FakeEntity& entity) const
        {
            const FakeVolume& volume = *entity.volume;
            services::VolumetricVolumeStatus status;
            status.entity = handleOf(id);
            status.name = entity.name;
            status.uuid = volume.uuid;
            const glm::vec3 center = entity.position + volume.data.centerOffset;
            status.worldMin = center - volume.data.halfExtents;
            status.worldMax = center + volume.data.halfExtents;
            status.voxelSize = volume.data.voxelSize;
            status.connectivity = volume.data.connectivity;
            status.agentClearance = volume.data.agentClearance;
            status.dims = volume.data.dims;
            status.baked = volume.data.baked;
            status.stale = volume.data.stale;
            status.navigablePercent = volume.data.navigablePercent;
            status.file = volume.data.bakedFile;
            status.bakeState = volume.bakeState;
            status.progress = volume.progress;
            return status;
        }

        FakeBake* findBake(uint64_t ticket)
        {
            for (FakeBake& bake : bakes)
            {
                if (bake.ticket == ticket)
                {
                    return &bake;
                }
            }
            return nullptr;
        }

        void registerHandlers()
        {
            auto& d = events::EventDispatcher::instance();

            // Modes and the project.
            d.registerQueryHandler<events::editor::IsPlayModeQuery>(
                [this](const events::editor::IsPlayModeQuery&) { return playMode; });
            d.registerQueryHandler<events::scene::IsSceneLoadPendingQuery>(
                [this](const events::scene::IsSceneLoadPendingQuery&) { return loadPending; });
            d.registerQueryHandler<events::world::IsWorldModeQuery>(
                [this](const events::world::IsWorldModeQuery&) { return worldMode; });
            d.registerQueryHandler<events::project::GetCurrentProjectQuery>(
                [](const events::project::GetCurrentProjectQuery&) -> std::optional<config::ProjectConfig>
                {
                    config::ProjectConfig config;
                    config.projectName = "McpVolumetric";
                    config.workingDirectory = projectRoot().string();
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
                    return entityData(it->first, it->second);
                });
            d.registerQueryHandler<events::scene::GetSceneHierarchyQuery>(
                [this](const events::scene::GetSceneHierarchyQuery&)
                {
                    services::SceneHierarchyData hierarchy;
                    hierarchy.root = handleOf(kRootId);
                    for (const auto& [id, entity] : entities)
                    {
                        hierarchy.entities.push_back(entityData(id, entity));
                    }
                    return hierarchy;
                });

            // Components.
            d.registerCommandHandler<events::scene::AddVolumetricNavVolumeComponentCommand>(
                [this](const events::scene::AddVolumetricNavVolumeComponentCommand& command)
                {
                    const auto it = entities.find(command.entity.id);
                    if (it == entities.end() || it->second.volume.has_value())
                    {
                        return false;
                    }
                    it->second.volume = FakeVolume{};
                    it->second.volume->uuid = 0xA000 + command.entity.id;
                    return true;
                });
            d.registerCommandHandler<events::scene::RemoveVolumetricNavVolumeComponentCommand>(
                [this](const events::scene::RemoveVolumetricNavVolumeComponentCommand& command)
                {
                    const auto it = entities.find(command.entity.id);
                    if (it == entities.end() || !it->second.volume.has_value())
                    {
                        return false;
                    }
                    it->second.volume.reset();
                    return true;
                });
            d.registerCommandHandler<events::scene::AddVolumetricAgentComponentCommand>(
                [this](const events::scene::AddVolumetricAgentComponentCommand& command)
                {
                    const auto it = entities.find(command.entity.id);
                    if (it == entities.end() || it->second.agent.has_value())
                    {
                        return false;
                    }
                    it->second.agent = FakeAgent{};
                    return true;
                });
            d.registerCommandHandler<events::scene::RemoveVolumetricAgentComponentCommand>(
                [this](const events::scene::RemoveVolumetricAgentComponentCommand& command)
                {
                    const auto it = entities.find(command.entity.id);
                    if (it == entities.end() || !it->second.agent.has_value())
                    {
                        return false;
                    }
                    it->second.agent.reset();
                    return true;
                });

            d.registerQueryHandler<events::volumetric::GetVolumetricNavVolumeDataQuery>(
                [this](const events::volumetric::GetVolumetricNavVolumeDataQuery& query)
                    -> std::optional<services::VolumetricNavVolumeData>
                {
                    const auto it = entities.find(query.entity.id);
                    if (it == entities.end() || !it->second.volume.has_value())
                    {
                        return std::nullopt;
                    }
                    return it->second.volume->data;
                });
            d.registerCommandHandler<events::volumetric::SetVolumetricNavVolumeDataCommand>(
                [this](const events::volumetric::SetVolumetricNavVolumeDataCommand& command)
                {
                    const auto it = entities.find(command.entity.id);
                    if (it == entities.end() || !it->second.volume.has_value() || !volumeAccepted(command.data))
                    {
                        return false;
                    }
                    ++volumeWrites;
                    services::VolumetricNavVolumeData& stored = it->second.volume->data;
                    stored.centerOffset = command.data.centerOffset;
                    stored.halfExtents = command.data.halfExtents;
                    stored.voxelSize = command.data.voxelSize;
                    stored.connectivity = command.data.connectivity;
                    stored.agentClearance = command.data.agentClearance;
                    stored.includeTerrain = command.data.includeTerrain;
                    stored.includeStaticMeshes = command.data.includeStaticMeshes;
                    stored.includeColliders = command.data.includeColliders;
                    return true;
                });
            d.registerQueryHandler<events::volumetric::GetVolumetricAgentDataQuery>(
                [this](const events::volumetric::GetVolumetricAgentDataQuery& query)
                    -> std::optional<services::VolumetricAgentData>
                {
                    const auto it = entities.find(query.entity.id);
                    if (it == entities.end() || !it->second.agent.has_value())
                    {
                        return std::nullopt;
                    }
                    return it->second.agent->data;
                });
            d.registerCommandHandler<events::volumetric::SetVolumetricAgentDataCommand>(
                [this](const events::volumetric::SetVolumetricAgentDataCommand& command)
                {
                    const auto it = entities.find(command.entity.id);
                    if (it == entities.end() || !it->second.agent.has_value() || !agentAccepted(command.data))
                    {
                        return false;
                    }
                    ++agentWrites;
                    services::VolumetricAgentData& stored = it->second.agent->data;
                    stored.radius = command.data.radius;
                    stored.maxSpeed = command.data.maxSpeed;
                    stored.maxAcceleration = command.data.maxAcceleration;
                    stored.stoppingDistance = command.data.stoppingDistance;
                    stored.turnSpeed = command.data.turnSpeed;
                    stored.alignPitch = command.data.alignPitch;
                    return true;
                });
            d.registerQueryHandler<events::volumetric::GetVolumetricAgentStateQuery>(
                [this](const events::volumetric::GetVolumetricAgentStateQuery& query)
                    -> std::optional<services::VolumetricAgentState>
                {
                    const auto it = entities.find(query.entity.id);
                    if (it == entities.end() || !it->second.agent.has_value())
                    {
                        return std::nullopt;
                    }
                    return it->second.agent->state;
                });

            // Volumes.
            d.registerQueryHandler<events::volumetric::GetVolumetricVolumeStatusQuery>(
                [this](const events::volumetric::GetVolumetricVolumeStatusQuery& query)
                    -> std::optional<services::VolumetricVolumeStatus>
                {
                    const auto it = entities.find(query.volume.id);
                    if (it == entities.end() || !it->second.volume.has_value())
                    {
                        return std::nullopt;
                    }
                    return statusOf(it->first, it->second);
                });
            d.registerQueryHandler<events::volumetric::ListVolumetricVolumesQuery>(
                [this](const events::volumetric::ListVolumetricVolumesQuery&)
                {
                    std::vector<services::VolumetricVolumeStatus> statuses;
                    for (const auto& [id, entity] : entities)
                    {
                        if (entity.volume.has_value())
                        {
                            statuses.push_back(statusOf(id, entity));
                        }
                    }
                    return statuses;
                });

            // Bakes.
            d.registerCommandHandler<events::volumetric::RequestVolumetricBakeCommand>(
                [this](const events::volumetric::RequestVolumetricBakeCommand& command)
                {
                    services::VolumetricBakeTicket ticket;
                    requested.push_back(command.volume.id);
                    if (const auto refusal = refusals.find(command.volume.id); refusal != refusals.end())
                    {
                        ticket.error = refusal->second;
                        return ticket;
                    }
                    FakeBake bake;
                    bake.ticket = nextTicket++;
                    bake.entity = command.volume.id;
                    bake.runningPolls = runningPolls;
                    if (const auto result = bakeResults.find(command.volume.id); result != bakeResults.end())
                    {
                        bake.result = result->second.first;
                        bake.message = result->second.second;
                    }
                    bakes.push_back(bake);
                    entities.at(command.volume.id).volume->bakeState = services::VolumetricBakeState::Running;
                    ticket.accepted = true;
                    ticket.ticket = bake.ticket;
                    return ticket;
                });
            d.registerQueryHandler<events::volumetric::GetVolumetricBakeOutcomeQuery>(
                [this](const events::volumetric::GetVolumetricBakeOutcomeQuery& query)
                {
                    ++outcomePolls;
                    services::VolumetricBakeOutcome outcome;
                    outcome.ticket = query.ticket;
                    FakeBake* bake = findBake(query.ticket);
                    if (bake == nullptr)
                    {
                        return outcome; // Unknown
                    }
                    outcome.volume = handleOf(bake->entity);
                    if (bake->cancelled)
                    {
                        outcome.state = services::VolumetricBakeState::Cancelled;
                        outcome.message = "the bake was cancelled";
                        return outcome;
                    }
                    if (bake->runningPolls > 0)
                    {
                        --bake->runningPolls;
                        outcome.state = services::VolumetricBakeState::Running;
                        outcome.progress = 0.5f;
                        return outcome;
                    }
                    outcome.state = bake->result;
                    outcome.message = bake->message;
                    outcome.durationMs = kBakeMs;
                    FakeVolume& volume = *entities.at(bake->entity).volume;
                    volume.bakeState = bake->result;
                    if (bake->result == services::VolumetricBakeState::Succeeded)
                    {
                        outcome.progress = 1.0f;
                        outcome.dims = glm::ivec3(kBakedEdge);
                        outcome.voxels = kBakedVoxels;
                        outcome.navigable = navigable;
                        volume.data.baked = true;
                        volume.data.dims = outcome.dims;
                        volume.data.voxels = kBakedVoxels;
                        volume.data.navigablePercent = 100.0f * static_cast<float>(navigable) / static_cast<float>(kBakedVoxels);
                    }
                    return outcome;
                });
            d.registerCommandHandler<events::volumetric::CancelVolumetricBakeCommand>(
                [this](const events::volumetric::CancelVolumetricBakeCommand& command)
                {
                    cancelled.push_back(command.volume.id);
                    for (FakeBake& bake : bakes)
                    {
                        if (!command.volume.isValid() || bake.entity == command.volume.id)
                        {
                            bake.cancelled = true;
                        }
                    }
                });
            d.registerCommandHandler<events::volumetric::SaveVolumetricVolumeCommand>(
                [this](const events::volumetric::SaveVolumetricVolumeCommand& command)
                {
                    services::VolumetricSaveResult result;
                    saved.push_back(command.volume.id);
                    saveDirectories.push_back(command.directory);
                    if (const auto error = saveErrors.find(command.volume.id); error != saveErrors.end())
                    {
                        result.error = error->second;
                        return result;
                    }
                    FakeEntity& entity = entities.at(command.volume.id);
                    result.success = true;
                    result.path = savedPath(entity.name);
                    entity.volume->data.bakedFile = result.path;
                    return result;
                });
            d.registerCommandHandler<events::volumetric::ClearVolumetricNavCommand>(
                [this](const events::volumetric::ClearVolumetricNavCommand& command)
                {
                    clears.push_back(command.volume.id);
                    for (auto& [id, entity] : entities)
                    {
                        if (!entity.volume.has_value() || (command.volume.isValid() && command.volume.id != id))
                        {
                            continue;
                        }
                        services::VolumetricNavVolumeData& data = entity.volume->data;
                        data.baked = false;
                        data.stale = false;
                        data.dims = glm::ivec3(0);
                        data.voxels = 0;
                        data.navigablePercent = 0.0f;
                        data.bakedFile.clear();
                    }
                });

            // Paths.
            d.registerQueryHandler<events::volumetric::FindPath3DQuery>(
                [this](const events::volumetric::FindPath3DQuery& query)
                {
                    ++pathQueries;
                    pathStart = query.start;
                    pathEnd = query.end;
                    pathSnapRadius = query.snapRadius;
                    return path;
                });
            d.registerQueryHandler<events::volumetric::IsPointNavigable3DQuery>(
                [this](const events::volumetric::IsPointNavigable3DQuery& query)
                {
                    return std::find(navigablePoints.begin(), navigablePoints.end(), query.point) != navigablePoints.end();
                });

            // Debug draw.
            d.registerQueryHandler<events::debugdraw::GetDebugDrawEnabledQuery>(
                [this](const events::debugdraw::GetDebugDrawEnabledQuery&) { return debugDrawEnabled; });
            // The volumetric service draws a shown path every frame: its path segments, then its snaps.
            d.registerCommandHandler<events::volumetric::ShowVolumetricPathCommand>(
                [this](const events::volumetric::ShowVolumetricPathCommand& command)
                {
                    ++pathShows;
                    shownSeconds = command.seconds;
                    for (std::size_t i = 1; i < command.waypoints.size(); ++i)
                    {
                        lines.emplace_back(command.waypoints[i - 1], command.waypoints[i]);
                    }
                    lines.insert(lines.end(), command.snaps.begin(), command.snaps.end());
                });

            // Undo: PushUndoableCommand stores without executing, like UndoRedoServiceImpl.
            d.registerCommandHandler<events::undoredo::PushUndoableCommand>(
                [this](const events::undoredo::PushUndoableCommand& command) { undoStack.push_back(command.command); });
        }
    };

    struct VolumetricFixture
    {
        DispatcherScope dispatcherScope;
        FakeVolumetricEngine engine;
        mcp::MainThreadQueue queue;
        mcp::ToolRegistry registry;

        VolumetricFixture()
        {
            engine.registerHandlers();
            engine.addEntity(kRootId, "Root");
            const mcp::tools::ToolContext context{queue};
            mcp::tools::registerVolumetricTools(registry, context);
            mcp::tools::registerComponentTools(registry, context);
        }

        VolumetricFixture(const VolumetricFixture&) = delete;
        VolumetricFixture& operator=(const VolumetricFixture&) = delete;

        const mcp::ToolDef& tool(const char* name) const
        {
            const auto found = registry.find(name);
            REQUIRE(found.get() != nullptr);
            return *found;
        }

        mcp::ToolResult call(const char* name, const json& args)
        {
            const mcp::ToolDef& def = tool(name);
            REQUIRE(def.affinity == mcp::ThreadAffinity::Main);
            return def.handler(args);
        }

        json callOk(const char* name, const json& args)
        {
            const mcp::ToolResult result = call(name, args);
            INFO(name << " failed: " << result.text);
            REQUIRE_FALSE(result.isError);
            return result.structured;
        }

        // Worker-affinity tools block on runOnMain; play the editor main thread here.
        mcp::ToolResult callWorker(const char* name, const json& args)
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

        // The failure of a call, reported in-band (isError) or thrown.
        std::string failure(const char* name, const json& args)
        {
            try
            {
                const mcp::ToolDef& def = tool(name);
                const mcp::ToolResult result =
                    def.affinity == mcp::ThreadAffinity::Worker ? callWorker(name, args) : call(name, args);
                return result.isError ? result.text : std::string("<no error>");
            }
            catch (const std::exception& e)
            {
                return e.what();
            }
        }

        // The message of an ArgError a Main tool throws.
        std::string argError(const char* name, const json& args)
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
    };

    // ------------------------------------------------------------------
    // Registration
    // ------------------------------------------------------------------

    TEST_CASE("volumetric tools: registering them dispatches nothing")
    {
        DispatcherScope scope; // no handlers at all: any dispatch would throw
        mcp::MainThreadQueue queue;
        mcp::ToolRegistry registry;
        const mcp::tools::ToolContext context{queue};
        CHECK_NOTHROW(mcp::tools::registerVolumetricTools(registry, context));
        CHECK(registry.size() == 4);
    }

    TEST_CASE("volumetric tools: affinity, annotations and closed schemas")
    {
        VolumetricFixture fixture;

        struct Expected
        {
            const char* name;
            mcp::ThreadAffinity affinity;
            bool readOnly;
            bool destructive;
        };
        const Expected expected[] = {
            {"volumetric_bake", mcp::ThreadAffinity::Worker, false, true},
            {"volumetric_get_info", mcp::ThreadAffinity::Main, true, false},
            {"volumetric_find_path", mcp::ThreadAffinity::Main, true, false},
            {"volumetric_clear", mcp::ThreadAffinity::Main, false, true},
        };
        for (const Expected& entry : expected)
        {
            INFO(entry.name);
            const mcp::ToolDef& def = fixture.tool(entry.name);
            CHECK(def.affinity == entry.affinity);
            CHECK(def.readOnly == entry.readOnly);
            CHECK(def.destructive == entry.destructive);
            CHECK(def.inputSchema.at("additionalProperties") == false);
        }
        CHECK(fixture.tool("volumetric_bake").timeout.count() >= 600000);
        CHECK(fixture.tool("volumetric_find_path").inputSchema.at("required") == json::array({"start", "end"}));
    }

    // ------------------------------------------------------------------
    // volumetric_bake
    // ------------------------------------------------------------------

    TEST_CASE("volumetric_bake bakes every volume, polls each ticket and saves every grid")
    {
        VolumetricFixture fixture;
        fixture.engine.addVolume(kVolumeId, "Sky", glm::vec3(0.0f, 20.0f, 0.0f), glm::vec3(30.0f, 15.0f, 30.0f));
        fixture.engine.addVolume(kSecondVolumeId, "Cave", glm::vec3(100.0f, 0.0f, 0.0f), glm::vec3(10.0f));
        fixture.engine.runningPolls = 2;

        const mcp::ToolResult result = fixture.callWorker("volumetric_bake", json::object());
        INFO(result.text);
        REQUIRE_FALSE(result.isError);
        CHECK((fixture.engine.requested == std::vector<uint64_t>{kVolumeId, kSecondVolumeId}));
        CHECK((fixture.engine.saved == std::vector<uint64_t>{kVolumeId, kSecondVolumeId}));
        CHECK((fixture.engine.saveDirectories == std::vector<std::string>{"", ""}));
        CHECK(fixture.engine.outcomePolls == 6); // per ticket: Running, Running, Succeeded
        CHECK(fixture.engine.cancelled.empty());

        const json& volumes = result.structured.at("volumes");
        REQUIRE(volumes.size() == 2);
        const json& sky = volumes.at(0);
        CHECK(sky.at("volume") == kVolumeId);
        CHECK(sky.at("name") == "Sky");
        CHECK(sky.at("dims") == json::array({10, 10, 10}));
        CHECK(sky.at("voxels") == 1000);
        CHECK(sky.at("navigablePercent") == 75.0);
        CHECK(sky.at("file") == "assets/volnav/Scene/Sky.vfVolNav");
        CHECK(sky.at("durationMs") == 12);
        CHECK(sky.at("warnings").empty());
        CHECK(volumes.at(1).at("volume") == kSecondVolumeId);
        CHECK(volumes.at(1).at("file") == "assets/volnav/Scene/Cave.vfVolNav");
        CHECK(textHas(result.structured.at("note").get<std::string>(), "scene_save"));
        CHECK(textHas(result.text, "Baked and saved 2 volumes: volume 10 ('Sky') 10x10x10, 75.0% navigable -> "
                                   "assets/volnav/Scene/Sky.vfVolNav"));

        // The volume now reports its grid and file.
        const json info = fixture.callOk("volumetric_get_info", {{"volume", kVolumeId}});
        REQUIRE(info.at("volumes").size() == 1);
        CHECK(info.at("volumes").at(0).at("baked") == true);
        CHECK(info.at("volumes").at(0).at("file") == "assets/volnav/Scene/Sky.vfVolNav");
        CHECK(info.at("volumes").at(0).at("bake").at("state") == "succeeded");
    }

    TEST_CASE("volumetric_bake {volume} bakes that volume only")
    {
        VolumetricFixture fixture;
        fixture.engine.addVolume(kVolumeId, "Sky", glm::vec3(0.0f), glm::vec3(30.0f, 15.0f, 30.0f));
        fixture.engine.addVolume(kSecondVolumeId, "Cave", glm::vec3(100.0f, 0.0f, 0.0f), glm::vec3(10.0f));

        const mcp::ToolResult result = fixture.callWorker("volumetric_bake", {{"volume", kSecondVolumeId}});
        INFO(result.text);
        REQUIRE_FALSE(result.isError);
        CHECK((fixture.engine.requested == std::vector<uint64_t>{kSecondVolumeId}));
        CHECK((fixture.engine.saved == std::vector<uint64_t>{kSecondVolumeId}));
        REQUIRE(result.structured.at("volumes").size() == 1);
        CHECK(result.structured.at("volumes").at(0).at("name") == "Cave");
    }

    TEST_CASE("volumetric_bake warns about a grid with no or every voxel navigable")
    {
        VolumetricFixture fixture;
        FakeVolume& volume = fixture.engine.addVolume(kVolumeId, "Sky", glm::vec3(0.0f), glm::vec3(30.0f, 15.0f, 30.0f));
        volume.data.agentClearance = 2.5f;

        SUBCASE("nothing navigable")
        {
            fixture.engine.navigable = 0;
            const mcp::ToolResult result = fixture.callWorker("volumetric_bake", json::object());
            REQUIRE_FALSE(result.isError);
            CHECK(listHas(result.structured.at("volumes").at(0).at("warnings"),
                          "no voxel is navigable: the box lies inside geometry, or agentClearance 2.5 m leaves no free "
                          "space"));
        }
        SUBCASE("everything navigable")
        {
            fixture.engine.navigable = kBakedVoxels;
            const mcp::ToolResult result = fixture.callWorker("volumetric_bake", json::object());
            REQUIRE_FALSE(result.isError);
            CHECK(listHas(result.structured.at("volumes").at(0).at("warnings"),
                          "every voxel is navigable: the bake found no geometry in the box"));
        }
    }

    TEST_CASE("volumetric_bake reports the volumes that did not bake and keeps the ones that did")
    {
        VolumetricFixture fixture;
        fixture.engine.addVolume(kVolumeId, "Sky", glm::vec3(0.0f), glm::vec3(30.0f, 15.0f, 30.0f));
        fixture.engine.addVolume(kSecondVolumeId, "Cave", glm::vec3(100.0f, 0.0f, 0.0f), glm::vec3(10.0f));

        SUBCASE("a failed bake")
        {
            fixture.engine.bakeResults[kVolumeId] = {services::VolumetricBakeState::Failed, "the volume's box is empty"};
            const mcp::ToolResult result = fixture.callWorker("volumetric_bake", json::object());
            CHECK(result.isError);
            CHECK(textHas(result.text, "1 of 2 volumes did not bake: volume 10 ('Sky'): the bake failed: the volume's "
                                       "box is empty."));
            CHECK(textHas(result.text, "Baked and saved: volume 11 ('Cave') 10x10x10, 75.0% navigable -> "
                                       "assets/volnav/Scene/Cave.vfVolNav; run scene_save"));
            CHECK((fixture.engine.saved == std::vector<uint64_t>{kSecondVolumeId}));
        }
        SUBCASE("a cancelled bake")
        {
            fixture.engine.bakeResults[kSecondVolumeId] = {services::VolumetricBakeState::Cancelled, ""};
            const mcp::ToolResult result = fixture.callWorker("volumetric_bake", json::object());
            CHECK(result.isError);
            CHECK(textHas(result.text, "volume 11 ('Cave'): the bake was cancelled"));
            CHECK((fixture.engine.saved == std::vector<uint64_t>{kVolumeId}));
        }
        SUBCASE("a grid that does not save")
        {
            fixture.engine.saveErrors[kVolumeId] = "could not write 'Sky.vfVolNav.tmp'";
            const mcp::ToolResult result = fixture.callWorker("volumetric_bake", json::object());
            CHECK(result.isError);
            CHECK(textHas(result.text, "volume 10 ('Sky'): baked, but NOT saved: could not write 'Sky.vfVolNav.tmp' (the "
                                       "grid stays loaded until the scene reloads)"));
            CHECK((fixture.engine.saved == std::vector<uint64_t>{kVolumeId, kSecondVolumeId}));
        }
    }

    TEST_CASE("volumetric_bake refuses a volume over the voxel limit and names a voxelSize that fits")
    {
        VolumetricFixture fixture;
        // 2000 x 1000 x 2000 m at voxelSize 1: 4e9 voxels.
        fixture.engine.addVolume(kVolumeId, "Huge", glm::vec3(0.0f), glm::vec3(1000.0f, 500.0f, 1000.0f));

        const std::string error = fixture.failure("volumetric_bake", json::object());
        CHECK(textHas(error, "Cannot bake volume 10 ('Huge'): the box needs 2000x1000x2000 = 4000000000 voxels at "
                             "voxelSize 1.0, more than the bake limit of 16777216: raise voxelSize to >= 6.22 or shrink "
                             "halfExtents (component_set VolumetricNavVolume). Nothing was baked."));
        CHECK(fixture.engine.requested.empty());

        // The hint is the smallest centimetre that fits.
        const std::size_t at = error.find(">= ");
        REQUIRE(at != std::string::npos);
        const float suggested = std::stof(error.substr(at + 3));
        const glm::vec3 min(-1000.0f, -500.0f, -1000.0f);
        const glm::vec3 max(1000.0f, 500.0f, 1000.0f);
        CHECK(::volumetric::voxelCountFor(min, max, suggested) <= ::volumetric::kMaxVoxels);
        CHECK(::volumetric::voxelCountFor(min, max, suggested - 0.01f) > ::volumetric::kMaxVoxels);
    }

    TEST_CASE("volumetric_bake is refused in Play mode, during a load, in World mode, without volumes and while one bakes")
    {
        VolumetricFixture fixture;
        FakeVolume& volume = fixture.engine.addVolume(kVolumeId, "Sky", glm::vec3(0.0f), glm::vec3(30.0f, 15.0f, 30.0f));

        SUBCASE("Play mode")
        {
            fixture.engine.playMode = true;
            CHECK(fixture.failure("volumetric_bake", json::object()) ==
                  "Cannot bake volumetric navigation in Play mode; call play_stop first.");
        }
        SUBCASE("a pending scene load")
        {
            fixture.engine.loadPending = true;
            CHECK(textHas(fixture.failure("volumetric_bake", json::object()), "while a scene load is in progress"));
        }
        SUBCASE("World mode")
        {
            fixture.engine.worldMode = true;
            CHECK(textHas(fixture.failure("volumetric_bake", json::object()),
                          "Cannot bake volumetric navigation in World mode"));
        }
        SUBCASE("no volume")
        {
            fixture.engine.entities.at(kVolumeId).volume.reset();
            CHECK(textHas(fixture.failure("volumetric_bake", json::object()),
                          "The scene has no VolumetricNavVolume to bake: component_add VolumetricNavVolume"));
        }
        SUBCASE("a bake already running")
        {
            volume.bakeState = services::VolumetricBakeState::Running;
            volume.progress = 0.4f;
            CHECK(textHas(fixture.failure("volumetric_bake", json::object()),
                          "A bake of volume 10 ('Sky') is already running (40%)"));
        }
        SUBCASE("an entity without a volume")
        {
            fixture.engine.addEntity(30, "Rock");
            CHECK(textHas(fixture.failure("volumetric_bake", {{"volume", 30}}),
                          "Entity 30 ('Rock', argument 'volume') has no VolumetricNavVolume"));
        }
        SUBCASE("a missing entity")
        {
            CHECK(textHas(fixture.failure("volumetric_bake", {{"volume", 99}}),
                          "Entity 99 (argument 'volume') does not exist"));
        }
        SUBCASE("an unknown argument")
        {
            CHECK_THROWS_AS((fixture.callWorker("volumetric_bake", {{"voxelSize", 2}})), mcp::ArgError);
        }
        CHECK(fixture.engine.requested.empty());
    }

    TEST_CASE("volumetric_bake cancels the bakes it started when a later request is refused")
    {
        VolumetricFixture fixture;
        fixture.engine.addVolume(kVolumeId, "Sky", glm::vec3(0.0f), glm::vec3(30.0f, 15.0f, 30.0f));
        fixture.engine.addVolume(kSecondVolumeId, "Cave", glm::vec3(100.0f, 0.0f, 0.0f), glm::vec3(10.0f));
        fixture.engine.refusals[kSecondVolumeId] = "the entity has no UUID";

        const std::string error = fixture.failure("volumetric_bake", json::object());
        CHECK(error == "The bake of volume 11 ('Cave') was refused: the entity has no UUID; the bakes this call had "
                       "started were cancelled");
        CHECK((fixture.engine.cancelled == std::vector<uint64_t>{kVolumeId}));
        CHECK(fixture.engine.outcomePolls == 0);
        CHECK(fixture.engine.saved.empty());
    }

    // ------------------------------------------------------------------
    // volumetric_get_info
    // ------------------------------------------------------------------

    TEST_CASE("volumetric_get_info reports the volumes and the flying agents")
    {
        VolumetricFixture fixture;
        FakeVolume& sky =
            fixture.engine.addBakedVolume(kVolumeId, "Sky", glm::vec3(0.0f, 20.0f, 0.0f), glm::vec3(30.0f, 15.0f, 30.0f));
        sky.bakeState = services::VolumetricBakeState::Succeeded;
        fixture.engine.addVolume(kSecondVolumeId, "Cave", glm::vec3(100.0f, 0.0f, 0.0f), glm::vec3(10.0f));

        FakeAgent& drone = fixture.engine.addAgent(kDroneId, "Drone", glm::vec3(0.0f, 20.0f, 0.0f), 0.8f);
        drone.data.registered = true;
        drone.state.state = services::VolumetricAgentMoveState::Moving;
        fixture.engine.addAgent(21, "Bat", glm::vec3(100.0f, 0.0f, 0.0f));
        fixture.engine.addAgent(22, "Lost", glm::vec3(500.0f, 0.0f, 0.0f));
        FakeAgent& hybrid = fixture.engine.addAgent(23, "Hybrid", glm::vec3(0.0f, 20.0f, 5.0f));
        hybrid.state.state = services::VolumetricAgentMoveState::Failed;
        hybrid.state.failReason = "OutsideVolume";
        fixture.engine.entities.at(23).navmeshAgent = true;

        SUBCASE("everything")
        {
            const json info = fixture.callOk("volumetric_get_info", json::object());
            const json& volumes = info.at("volumes");
            REQUIRE(volumes.size() == 2);

            const json& skyInfo = volumes.at(0);
            CHECK(skyInfo.at("entity") == kVolumeId);
            CHECK(skyInfo.at("name") == "Sky");
            CHECK(skyInfo.at("worldBounds").at("min") == json::array({-30.0, 5.0, -30.0}));
            CHECK(skyInfo.at("worldBounds").at("max") == json::array({30.0, 35.0, 30.0}));
            CHECK(skyInfo.at("voxelSize") == 1.0);
            CHECK(skyInfo.at("connectivity") == 26);
            CHECK(skyInfo.at("clearance") == 0.5);
            CHECK(skyInfo.at("baked") == true);
            CHECK(skyInfo.at("stale") == false);
            CHECK(skyInfo.at("dims") == json::array({10, 10, 10}));
            CHECK(skyInfo.at("navigablePercent") == 75.0);
            CHECK(skyInfo.at("file") == "assets/volnav/Scene/Sky.vfVolNav");
            CHECK(skyInfo.at("bake").at("state") == "succeeded");
            CHECK(skyInfo.at("warnings").empty());

            const json& caveInfo = volumes.at(1);
            CHECK(caveInfo.at("baked") == false);
            CHECK(caveInfo.at("file") == "");
            CHECK(caveInfo.at("bake").at("state") == "none");

            const json& agents = info.at("agents");
            REQUIRE(agents.size() == 4);
            const json& droneInfo = agents.at(0);
            CHECK(droneInfo.at("entity") == kDroneId);
            CHECK(droneInfo.at("registered") == true);
            CHECK(droneInfo.at("state") == "moving");
            CHECK(droneInfo.at("inVolume") == kVolumeId);
            CHECK(droneInfo.at("position") == json::array({0.0, 20.0, 0.0}));
            CHECK(listHas(droneInfo.at("warnings"), "radius 0.8 is larger than the agentClearance 0.5 of volume 10 "
                                                    "('Sky')"));

            CHECK(agents.at(1).at("inVolume").is_null());
            CHECK(listHas(agents.at(1).at("warnings"), "volume 11 ('Cave') holds it but is not baked"));
            CHECK(listHas(agents.at(2).at("warnings"), "outside every baked VolumetricNavVolume, so it cannot fly: move it"));

            const json& hybridInfo = agents.at(3);
            CHECK(hybridInfo.at("state") == "failed");
            CHECK(hybridInfo.at("failReason") == "OutsideVolume");
            CHECK(hybridInfo.at("inVolume") == kVolumeId);
            CHECK(listHas(hybridInfo.at("warnings"), "the entity also has a NavmeshAgent"));
            CHECK_FALSE(droneInfo.contains("failReason"));
        }
        SUBCASE("one volume and the agents inside its box")
        {
            const json info = fixture.callOk("volumetric_get_info", {{"volume", kSecondVolumeId}});
            REQUIRE(info.at("volumes").size() == 1);
            CHECK(info.at("volumes").at(0).at("name") == "Cave");
            REQUIRE(info.at("agents").size() == 1);
            CHECK(info.at("agents").at(0).at("name") == "Bat");
        }
        SUBCASE("an entity without a volume")
        {
            CHECK(textHas(fixture.failure("volumetric_get_info", {{"volume", kDroneId}}),
                          "Entity 20 ('Drone', argument 'volume') has no VolumetricNavVolume"));
        }
        SUBCASE("a volume past the voxel limit, stale or unsaved")
        {
            sky.data.voxelSize = 0.1f; // 600 x 300 x 600 voxels
            sky.data.stale = true;
            sky.data.bakedFile.clear();
            const json info = fixture.callOk("volumetric_get_info", {{"volume", kVolumeId}});
            const json& warnings = info.at("volumes").at(0).at("warnings");
            CHECK(listHas(warnings, "volumetric_bake refuses this volume: the box needs 600x300x600"));
            CHECK(listHas(warnings, "stale: the box or the bake settings changed"));
            CHECK(listHas(warnings, "never saved to a .vfVolNav"));
        }
    }

    // ------------------------------------------------------------------
    // volumetric_find_path
    // ------------------------------------------------------------------

    TEST_CASE("volumetric_find_path reports the path, the snapping and why a search fails")
    {
        VolumetricFixture fixture;
        FakeVolume& sky =
            fixture.engine.addBakedVolume(kVolumeId, "Sky", glm::vec3(0.0f, 20.0f, 0.0f), glm::vec3(30.0f, 15.0f, 30.0f));
        fixture.engine.addAgent(kDroneId, "Drone", glm::vec3(0.0f, 20.0f, 0.0f));

        ::volumetric::VolumePathResult found;
        found.status = ::volumetric::VolumePathStatus::Found;
        found.waypoints = {glm::vec3(0.0f, 20.0f, 0.0f), glm::vec3(0.0f, 25.0f, 10.0f), glm::vec3(10.0f, 25.0f, 10.0f)};
        found.startSnapped = glm::vec3(0.0f, 20.0f, 0.0f);
        found.nodesExpanded = 42;
        found.volumeId = sky.uuid;
        fixture.engine.navigablePoints = {glm::vec3(10.0f, 25.0f, 10.0f)};

        SUBCASE("a path with a snapped start")
        {
            fixture.engine.path = found;
            const json result =
                fixture.callOk("volumetric_find_path", {{"start", {0.0, 19.0, 0.0}}, {"end", {10.0, 25.0, 10.0}}});
            CHECK(fixture.engine.pathQueries == 1);
            CHECK(fixture.engine.pathStart == glm::vec3(0.0f, 19.0f, 0.0f));
            CHECK(fixture.engine.pathEnd == glm::vec3(10.0f, 25.0f, 10.0f));
            CHECK(fixture.engine.pathSnapRadius == 2.0f);

            CHECK(result.at("found") == true);
            CHECK(result.at("partial") == false);
            CHECK(result.at("status") == "Found");
            CHECK_FALSE(result.contains("reason"));
            CHECK(result.at("volume") == kVolumeId);
            CHECK(result.at("path").size() == 3);
            CHECK(result.at("path").at(1) == json::array({0.0, 25.0, 10.0}));
            CHECK(result.at("length").get<double>() == doctest::Approx(std::sqrt(125.0) + 10.0).epsilon(1e-5));
            CHECK(result.at("nodesExpanded") == 42);

            const json& start = result.at("start");
            CHECK(start.at("given") == json::array({0.0, 19.0, 0.0}));
            CHECK(start.at("navigable") == false);
            CHECK(start.at("snapped") == json::array({0.0, 20.0, 0.0}));
            CHECK(start.at("offset") == 1.0);
            CHECK_FALSE(start.contains("entity"));

            const json& end = result.at("end");
            CHECK(end.at("navigable") == true);
            CHECK_FALSE(end.contains("snapped"));
            CHECK(end.at("offset") == 0.0);
            CHECK(result.at("warnings").empty());
            CHECK_FALSE(result.contains("shown"));
            CHECK(fixture.engine.lines.empty());
        }
        SUBCASE("an entity endpoint is its world position")
        {
            fixture.engine.path = found;
            const json result =
                fixture.callOk("volumetric_find_path", {{"start", kDroneId}, {"end", {10.0, 25.0, 10.0}}, {"snapRadius", 4}});
            CHECK(fixture.engine.pathStart == glm::vec3(0.0f, 20.0f, 0.0f));
            CHECK(fixture.engine.pathSnapRadius == 4.0f);
            CHECK(result.at("start").at("entity") == kDroneId);
            CHECK(result.at("start").at("given") == json::array({0.0, 20.0, 0.0}));
        }
        SUBCASE("a partial path says how far short it ends")
        {
            fixture.engine.path.status = ::volumetric::VolumePathStatus::Partial;
            fixture.engine.path.waypoints = {glm::vec3(0.0f, 20.0f, 0.0f), glm::vec3(5.0f, 20.0f, 0.0f)};
            fixture.engine.path.volumeId = sky.uuid;
            const json result =
                fixture.callOk("volumetric_find_path", {{"start", {0.0, 20.0, 0.0}}, {"end", {10.0, 20.0, 0.0}}});
            CHECK(result.at("found") == false);
            CHECK(result.at("partial") == true);
            CHECK(textHas(result.at("reason").get<std::string>(),
                          "the path ends at the reachable point nearest to the end, 5.0 m short of it"));
            CHECK(result.at("end").at("offset") == 5.0);
            CHECK(result.at("length") == 5.0);
        }
        SUBCASE("a blocked start")
        {
            fixture.engine.path.status = ::volumetric::VolumePathStatus::StartBlocked;
            fixture.engine.path.volumeId = sky.uuid;
            const json result = fixture.callOk("volumetric_find_path",
                                               {{"start", {0.0, 20.0, 0.0}}, {"end", {10.0, 25.0, 10.0}}, {"snapRadius", 1.5}});
            CHECK(result.at("status") == "StartBlocked");
            CHECK(result.at("reason") == "the start lies in blocked space (inside geometry, or closer to it than the "
                                         "volume's agentClearance) and no navigable voxel is within snapRadius 1.5 m: move "
                                         "it, or raise snapRadius (up to 10)");
            CHECK(result.at("path").empty());
            CHECK(result.at("length") == 0.0);
            CHECK(result.at("start").at("offset").is_null());
            CHECK(result.at("volume") == kVolumeId);
        }
        SUBCASE("an end outside every baked volume")
        {
            fixture.engine.path.status = ::volumetric::VolumePathStatus::OutsideVolume;
            const json result =
                fixture.callOk("volumetric_find_path", {{"start", {0.0, 20.0, 0.0}}, {"end", {500.0, 0.0, 0.0}}});
            CHECK(textHas(result.at("reason").get<std::string>(), "the end lies outside every baked volume"));
            CHECK(result.at("volume").is_null());
        }
        SUBCASE("no baked volume")
        {
            fixture.engine.path.status = ::volumetric::VolumePathStatus::NoVolume;
            const json result =
                fixture.callOk("volumetric_find_path", {{"start", {0.0, 20.0, 0.0}}, {"end", {1.0, 20.0, 0.0}}});
            CHECK(textHas(result.at("reason").get<std::string>(), "no VolumetricNavVolume is baked"));
        }
        SUBCASE("a stale volume")
        {
            sky.data.stale = true;
            fixture.engine.path = found;
            const json result =
                fixture.callOk("volumetric_find_path", {{"start", {0.0, 19.0, 0.0}}, {"end", {10.0, 25.0, 10.0}}});
            CHECK(listHas(result.at("warnings"), "volume 10 ('Sky') is stale"));
        }
        SUBCASE("show draws the path and the snap")
        {
            fixture.engine.path = found;
            const json result = fixture.callOk("volumetric_find_path",
                                               {{"start", {0.0, 19.0, 0.0}}, {"end", {10.0, 25.0, 10.0}}, {"show", true}});
            CHECK(result.at("shown") == true);
            CHECK(fixture.engine.pathShows == 1);
            CHECK(fixture.engine.shownSeconds == doctest::Approx(10.0f)); // the default showSeconds
            REQUIRE(fixture.engine.lines.size() == 3); // two path segments, one snap
            CHECK(fixture.engine.lines.at(0).first == glm::vec3(0.0f, 20.0f, 0.0f));
            CHECK(fixture.engine.lines.at(1).second == glm::vec3(10.0f, 25.0f, 10.0f));
            CHECK(fixture.engine.lines.at(2).first == glm::vec3(0.0f, 19.0f, 0.0f));
            CHECK(fixture.engine.lines.at(2).second == glm::vec3(0.0f, 20.0f, 0.0f));
        }
        SUBCASE("showSeconds sets how long the path stays drawn")
        {
            fixture.engine.path = found;
            fixture.callOk("volumetric_find_path", {{"start", {0.0, 19.0, 0.0}}, {"end", {10.0, 25.0, 10.0}},
                                                    {"show", true}, {"showSeconds", 30}});
            CHECK(fixture.engine.pathShows == 1);
            CHECK(fixture.engine.shownSeconds == doctest::Approx(30.0f));
            const json end = json::array({10.0, 25.0, 10.0});
            CHECK(textHas(fixture.argError("volumetric_find_path", {{"start", {0.0, 19.0, 0.0}}, {"end", end},
                                                                    {"show", true}, {"showSeconds", 0.5}}),
                          "argument 'showSeconds' must be between 1.0 and 60.0"));
        }
        SUBCASE("show with debug drawing off or without a path draws nothing")
        {
            SUBCASE("debug drawing off")
            {
                fixture.engine.path = found;
                fixture.engine.debugDrawEnabled = false;
                const json result = fixture.callOk("volumetric_find_path",
                                                   {{"start", {0.0, 19.0, 0.0}}, {"end", {10.0, 25.0, 10.0}}, {"show", true}});
                CHECK(result.at("shown") == false);
                CHECK(listHas(result.at("warnings"), "debug drawing is disabled"));
            }
            SUBCASE("no path")
            {
                fixture.engine.path.status = ::volumetric::VolumePathStatus::NoPath;
                const json result = fixture.callOk("volumetric_find_path",
                                                   {{"start", {0.0, 19.0, 0.0}}, {"end", {10.0, 25.0, 10.0}}, {"show", true}});
                CHECK(result.at("shown") == false);
                CHECK(listHas(result.at("warnings"), "show: there is no path to draw"));
            }
            CHECK(fixture.engine.lines.empty());
        }
        SUBCASE("bad arguments")
        {
            const json end = json::array({10.0, 25.0, 10.0});
            CHECK(textHas(fixture.argError("volumetric_find_path", {{"start", {0.0, 19.0, 0.0}}, {"end", end}, {"snapRadius", 11}}),
                          "argument 'snapRadius' must be between 0.0 and 10.0"));
            CHECK(textHas(fixture.argError("volumetric_find_path", {{"start", "here"}, {"end", end}}),
                          "argument 'start' must be an entity id or a world point [x, y, z]"));
            CHECK(textHas(fixture.argError("volumetric_find_path", {{"start", {1.0, 2.0}}, {"end", end}}),
                          "'start' must be an array of 3 numbers"));
            CHECK(textHas(fixture.argError("volumetric_find_path", {{"start", {0.0, 19.0, 0.0}}}),
                          "missing required argument 'end'"));
            CHECK(textHas(fixture.argError("volumetric_find_path", {{"start", {0.0, 19.0, 0.0}}, {"end", end}, {"radius", 1}}),
                          "unknown argument 'radius' for volumetric_find_path"));
            CHECK(textHas(fixture.failure("volumetric_find_path", {{"start", 99}, {"end", end}}),
                          "Entity 99 (argument 'start') does not exist"));
            CHECK(fixture.engine.pathQueries == 0);
        }
    }

    // ------------------------------------------------------------------
    // volumetric_clear
    // ------------------------------------------------------------------

    TEST_CASE("volumetric_clear unloads one volume or every volume and keeps the files")
    {
        VolumetricFixture fixture;
        fixture.engine.addBakedVolume(kVolumeId, "Sky", glm::vec3(0.0f), glm::vec3(30.0f, 15.0f, 30.0f));
        fixture.engine.addBakedVolume(kSecondVolumeId, "Cave", glm::vec3(100.0f, 0.0f, 0.0f), glm::vec3(10.0f));

        SUBCASE("one volume")
        {
            const json result = fixture.callOk("volumetric_clear", {{"volume", kVolumeId}});
            REQUIRE(result.at("cleared").size() == 1);
            const json& entry = result.at("cleared").at(0);
            CHECK(entry.at("volume") == kVolumeId);
            CHECK(entry.at("name") == "Sky");
            CHECK(entry.at("wasBaked") == true);
            CHECK(entry.at("file") == "assets/volnav/Scene/Sky.vfVolNav");
            CHECK(textHas(result.at("note").get<std::string>(), "Not undoable"));
            CHECK(textHas(result.at("note").get<std::string>(), "scene_save"));
            CHECK((fixture.engine.clears == std::vector<uint64_t>{kVolumeId}));
            CHECK_FALSE(fixture.engine.entities.at(kVolumeId).volume->data.baked);
            CHECK(fixture.engine.entities.at(kSecondVolumeId).volume->data.baked);
        }
        SUBCASE("every volume")
        {
            const json result = fixture.callOk("volumetric_clear", json::object());
            CHECK(result.at("cleared").size() == 2);
            CHECK((fixture.engine.clears == std::vector<uint64_t>{services::EntityHandle::INVALID_ID}));
        }
        SUBCASE("refused in Play mode")
        {
            fixture.engine.playMode = true;
            CHECK(fixture.failure("volumetric_clear", json::object()) ==
                  "Cannot clear volumetric navigation in Play mode; call play_stop first.");
            CHECK(fixture.engine.clears.empty());
        }
        SUBCASE("an entity without a volume")
        {
            fixture.engine.addEntity(30, "Rock");
            CHECK(textHas(fixture.failure("volumetric_clear", {{"volume", 30}}), "has no VolumetricNavVolume"));
            CHECK(fixture.engine.clears.empty());
        }
    }

    TEST_CASE("volumetric_clear in a scene without volumes clears nothing")
    {
        VolumetricFixture fixture;
        const mcp::ToolResult result = fixture.call("volumetric_clear", json::object());
        REQUIRE_FALSE(result.isError);
        CHECK(result.structured.at("cleared").empty());
        CHECK(fixture.engine.clears.empty());
    }

    // ------------------------------------------------------------------
    // The component bindings
    // ------------------------------------------------------------------

    TEST_CASE("VolumetricAgent: component_add refuses an entity with a NavmeshAgent")
    {
        VolumetricFixture fixture;
        fixture.engine.addEntity(30, "Walker").navmeshAgent = true;

        const mcp::ToolResult refused = fixture.call("component_add", {{"entity", 30}, {"type", "VolumetricAgent"}});
        CHECK(refused.isError);
        CHECK(textHas(refused.text, "AddVolumetricAgentComponent failed: the entity has a NavmeshAgent"));
        CHECK(textHas(refused.text, "component_remove the NavmeshAgent first"));
        CHECK_FALSE(fixture.engine.entities.at(30).agent.has_value());
        CHECK(fixture.engine.undoStack.empty());

        fixture.engine.entities.at(30).navmeshAgent = false;
        const json added = fixture.callOk("component_add",
                                          {{"entity", 30}, {"type", "VolumetricAgent"}, {"data", {{"radius", 0.75}}}});
        const json& data = added.at("data");
        CHECK(data.at("radius") == 0.75);
        CHECK(data.at("registered") == false);
        CHECK(data.at("state") == "idle");
        CHECK(listHas(data.at("warnings"), "the scene has no VolumetricNavVolume, so it cannot fly"));
        CHECK(fixture.engine.entities.at(30).agent->data.radius == 0.75f);
        CHECK(fixture.engine.undoStack.size() == 1);
    }

    TEST_CASE("VolumetricAgent binding: Play state, read-only fields, ranges and the generic undo")
    {
        VolumetricFixture fixture;
        fixture.engine.addBakedVolume(kVolumeId, "Sky", glm::vec3(0.0f, 20.0f, 0.0f), glm::vec3(30.0f, 15.0f, 30.0f));
        FakeAgent& drone = fixture.engine.addAgent(kDroneId, "Drone", glm::vec3(0.0f, 20.0f, 0.0f));
        drone.data.registered = true;
        drone.state.state = services::VolumetricAgentMoveState::Failed;
        drone.state.failReason = "Stuck";
        const json target{{"entity", kDroneId}, {"type", "VolumetricAgent"}};

        const json got = fixture.callOk("component_get", target).at("data");
        CHECK(got.at("radius") == 0.5);
        CHECK(got.at("turnSpeed") == 360.0);
        CHECK(got.at("alignPitch") == false);
        CHECK(got.at("registered") == true);
        CHECK(got.at("state") == "failed");
        CHECK(listHas(got.at("warnings"), "its last move failed: Stuck"));

        json readOnly = target;
        readOnly["data"] = {{"state", "idle"}};
        CHECK(textHas(fixture.argError("component_set", readOnly),
                      "field 'state' of VolumetricAgent is read-only. Writable fields: radius"));

        json tooFast = target;
        tooFast["data"] = {{"maxSpeed", 51}};
        CHECK(textHas(fixture.argError("component_set", tooFast), "'data.maxSpeed' must be between 0.0 and 50.0"));

        json set = target;
        set["data"] = {{"radius", 1.25}, {"alignPitch", true}};
        const json written = fixture.callOk("component_set", set).at("data");
        CHECK(written.at("radius") == 1.25);
        CHECK(written.at("alignPitch") == true);
        CHECK(listHas(written.at("warnings"), "radius 1.25 is larger than the agentClearance 0.5 of volume 10 ('Sky')"));
        REQUIRE(fixture.engine.undoStack.size() == 1);

        fixture.engine.undoStack.back()->undo();
        CHECK(drone.data.radius == 0.5f);
        CHECK(drone.data.alignPitch == false);
    }

    TEST_CASE("VolumetricNavVolume binding: component_add, read-only fields, ranges and the generic undo")
    {
        VolumetricFixture fixture;
        fixture.engine.addEntity(40, "Airspace", glm::vec3(0.0f, 50.0f, 0.0f));
        const json target{{"entity", 40}, {"type", "VolumetricNavVolume"}};

        json add = target;
        add["data"] = {{"halfExtents", {1000, 500, 1000}}};
        const json added = fixture.callOk("component_add", add).at("data");
        CHECK(added.at("halfExtents") == json::array({1000.0, 500.0, 1000.0}));
        CHECK(added.at("voxelSize") == 1.0);
        CHECK(added.at("connectivity") == 26);
        CHECK(added.at("baked") == false);
        CHECK(added.at("stale") == false);
        CHECK(added.at("dims") == json::array({0, 0, 0}));
        CHECK(added.at("voxels") == 0);
        CHECK(added.at("bakedFile") == "");
        CHECK(added.at("worldBounds").at("min") == json::array({-1000.0, -450.0, -1000.0}));
        CHECK(added.at("worldBounds").at("max") == json::array({1000.0, 550.0, 1000.0}));
        CHECK(listHas(added.at("warnings"), "volumetric_bake refuses this volume: the box needs 2000x1000x2000"));
        REQUIRE(fixture.engine.undoStack.size() == 1);

        SUBCASE("read-only fields are refused")
        {
            json set = target;
            set["data"] = {{"baked", true}};
            CHECK(textHas(fixture.argError("component_set", set),
                          "field 'baked' of VolumetricNavVolume is read-only. Writable fields: centerOffset"));
        }
        SUBCASE("out-of-range, invalid and unknown values are refused")
        {
            json set = target;
            set["data"] = {{"voxelSize", 0.05}};
            CHECK(textHas(fixture.argError("component_set", set), "'data.voxelSize' must be between 0.1 and 10.0"));
            set["data"] = {{"connectivity", 7}};
            CHECK(textHas(fixture.argError("component_set", set),
                          "connectivity must be 6 (steps through voxel faces only) or 26"));
            set["data"] = {{"bogus", 1}};
            CHECK(textHas(fixture.argError("component_set", set), "unknown data key 'bogus'"));
            CHECK(fixture.engine.undoStack.size() == 1);
        }
        SUBCASE("component_set writes once and the generic undo restores the previous values")
        {
            const int writesBefore = fixture.engine.volumeWrites;
            json set = target;
            set["data"] = {{"voxelSize", 8}, {"connectivity", 6}};
            const json written = fixture.callOk("component_set", set).at("data");
            CHECK(written.at("voxelSize") == 8.0);
            CHECK(written.at("connectivity") == 6);
            CHECK(written.at("warnings").empty()); // 250 x 125 x 250 voxels fit
            CHECK(fixture.engine.volumeWrites == writesBefore + 1);
            REQUIRE(fixture.engine.undoStack.size() == 2);

            fixture.engine.undoStack.back()->undo();
            const services::VolumetricNavVolumeData& stored = fixture.engine.entities.at(40).volume->data;
            CHECK(stored.voxelSize == 1.0f);
            CHECK(stored.connectivity == 26);
        }
        SUBCASE("a component_get passed back to component_set writes nothing")
        {
            const json current = fixture.callOk("component_get", target).at("data");
            json writable = current;
            for (const char* field : {"baked", "stale", "dims", "voxels", "navigablePercent", "bakedFile", "worldBounds",
                                      "warnings"})
            {
                writable.erase(field);
            }
            const int writesBefore = fixture.engine.volumeWrites;
            json set = target;
            set["data"] = writable;
            fixture.callOk("component_set", set);
            CHECK(fixture.engine.volumeWrites == writesBefore);
        }
        SUBCASE("component_remove and its undo")
        {
            json remove = target;
            fixture.callOk("component_remove", remove);
            CHECK_FALSE(fixture.engine.entities.at(40).volume.has_value());
            REQUIRE(fixture.engine.undoStack.size() == 2);
            fixture.engine.undoStack.back()->undo();
            REQUIRE(fixture.engine.entities.at(40).volume.has_value());
            CHECK(fixture.engine.entities.at(40).volume->data.halfExtents == glm::vec3(1000.0f, 500.0f, 1000.0f));
        }
    }
}
