#include <doctest.h>
#include <ostream> // doctest stringifies std::string_view and nlohmann::json through operator<<

#include "dispatch/MainThreadQueue.hpp"
#include "protocol/ArgReader.hpp"
#include "protocol/ToolRegistry.hpp"
#include "tools/CoreTools.hpp"
#include "undo/EntityIdRemap.hpp"

#include "asset/AssetDatabase.hpp"
#include "behaviortree/BehaviorTreeTypes.hpp"
#include "components/Components.hpp"
#include "scene/EntityRegistry.hpp"
#include "scene/SceneGraphSystem.hpp"

#include "data/BehaviorTreeComponentData.hpp"
#include "data/DTOs.hpp"
#include "data/UndoTypes.hpp"
#include "events/EventDispatcher.hpp"
#include "events/ai/BehaviorTreeComponentEvents.hpp"
#include "events/ai/BehaviorTreeEvents.hpp"
#include "events/editor/EditorModeEvents.hpp"
#include "events/editor/UndoRedoEvents.hpp"
#include "events/navmesh/NavmeshAuthoringEvents.hpp"
#include "events/navmesh/NavmeshEvents.hpp"
#include "events/project/ProjectEvents.hpp"
#include "events/render/RenderEvents.hpp"
#include "events/scene/ComponentMediaEvents.hpp"
#include "events/scene/ComponentPhysicsLightEvents.hpp"
#include "events/scene/EntityTransformEvents.hpp"
#include "events/scene/ScenePersistenceEvents.hpp"
#include "events/scene/SceneSettingsEvents.hpp"
#include "events/world/WorldSectorEvents.hpp"

#include "impl/components/PhysicsComponentService.hpp"
#include "impl/scene/EntityQueryService.hpp"
#include "impl/scene/EntityStateService.hpp"
#include "impl/scene/HierarchyService.hpp"
#include "impl/scene/ScenePersistenceService.hpp"
#include "impl/scene/TransformComponentService.hpp"

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <future>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <system_error>
#include <vector>

// VK-1660: the navigation tools (navmesh_*, route_*) and the NavmeshAgent binding. The tools run against the REAL
// scene services over a private scene graph (hierarchy, entity queries and state, transforms, prefab JSON for the
// creation undo, the NavRoute / NavmeshAgent add and remove of PhysicsComponentService) and a FAKE navmesh service:
// a flat square navmesh at y = 0 with a scripted ticketed bake, so polling, refusals, the saved folder, the find-path
// reasons and the agent warnings are deterministic. The undo stack records what the tools push without running it.
namespace
{
    namespace fs = std::filesystem;
    using json = nlohmann::json;

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

    services::EntityHandle handleOf(uint32_t id)
    {
        services::EntityHandle handle;
        handle.id = static_cast<uint64_t>(id);
        return handle;
    }

    entt::entity enttOf(uint32_t id)
    {
        return static_cast<entt::entity>(id);
    }

    entt::registry& reg()
    {
        return scene::EntityRegistry::getRegistry();
    }

    bool alive(uint32_t id)
    {
        return reg().valid(enttOf(id));
    }

    bool contains(const std::string& text, const std::string& needle)
    {
        return text.find(needle) != std::string::npos;
    }

    bool anyContains(const json& lines, const std::string& needle)
    {
        for (const json& line : lines)
        {
            if (line.is_string() && contains(line.get<std::string>(), needle))
            {
                return true;
            }
        }
        return false;
    }

    std::size_t batchSize(const std::shared_ptr<services::IUndoableCommand>& entry)
    {
        const auto* batch = dynamic_cast<const services::BatchUndoCommand*>(entry.get());
        return batch != nullptr ? batch->getCommandCount() : 0;
    }

    json point(double x, double y, double z)
    {
        return json::array({x, y, z});
    }

    std::string nameOf(entt::entity entity)
    {
        return reg().get<components::NameComponent>(entity).name;
    }

    glm::vec3 localPosition(entt::entity entity)
    {
        return reg().get<components::TransformComponent>(entity).position;
    }

    // The navmesh: the square |x|, |z| <= extent at y = 0. GetClosestPoint finds the clamped point inside its search
    // box and otherwise ECHOES its input, as NavmeshAdapter does. A wall at x = wallX splits the square: a path across
    // it stops 1 m before the wall, flagged isPartial unless `hierarchical` (findPathHierarchical never sets it).
    struct FakeNavmesh
    {
        bool mesh = false;
        float extent = 50.0f;
        bool externalBake = false;  // a bake started elsewhere (the Navigation window)
        types::NavmeshBakeSettings settings;
        std::string indexPath;
        bool unsavedChanges = false;
        navigation::NavmeshStats stats;

        // The ticketed bake.
        bool refuseBake = false;
        std::string refusal = "No geometry found in scene";
        int runningPolls = 2;
        events::navmesh::NavmeshBakeState finalState = events::navmesh::NavmeshBakeState::Succeeded;
        std::string failure = "Rasterization failed";
        navigation::NavmeshStats bakedStats{4, 120, 300, glm::vec3(-64.0f, 0.0f, -64.0f), glm::vec3(64.0f, 2.0f, 64.0f)};
        bool saveResult = true;
        std::vector<events::navmesh::RequestNavmeshBakeCommand> requests;
        std::vector<std::string> savedDirectories;
        uint64_t ticket = 0;
        int polls = 0;
        int outcomePolls = 0;
        bool running = false;
        int clears = 0;
        bool overlay = false;
        int overlayUploads = 0;

        // Paths.
        bool connected = true;
        std::optional<float> wallX;
        bool hierarchical = false;
        std::vector<events::navmesh::FindPathQuery> pathQueries;

        // NavmeshAgent data (NavmeshAgentManager's rules) and the entities registered with the crowd.
        std::set<uint64_t> crowd;

        glm::vec3 clamped(const glm::vec3& p) const
        {
            return {std::clamp(p.x, -extent, extent), 0.0f, std::clamp(p.z, -extent, extent)};
        }

        static bool withinBox(const glm::vec3& a, const glm::vec3& b, float half)
        {
            return std::fabs(a.x - b.x) <= half && std::fabs(a.y - b.y) <= half && std::fabs(a.z - b.z) <= half;
        }

        static bool usable(const services::NavmeshAgentData& data)
        {
            return data.radius > 0.0f && data.height > 0.0f && data.maxSpeed >= 0.0f && data.maxAcceleration >= 0.0f &&
                   data.turnSpeed >= 0.0f && data.stoppingDistance >= 0.0f && data.rootMotionSpeedScale >= 0.0f &&
                   data.avoidanceQuality >= 0 && data.avoidanceQuality <= 3;
        }

        void registerHandlers()
        {
            auto& d = events::EventDispatcher::instance();
            using namespace events::navmesh;

            d.registerQueryHandler<HasNavmeshQuery>([this](const HasNavmeshQuery&) { return mesh; });
            d.registerQueryHandler<IsPointOnNavmeshQuery>([this](const IsPointOnNavmeshQuery& query)
            {
                return mesh && withinBox(clamped(query.point), query.point, query.tolerance);
            });
            d.registerQueryHandler<GetClosestPointQuery>([this](const GetClosestPointQuery& query)
            {
                const glm::vec3 candidate = clamped(query.point);
                return mesh && withinBox(candidate, query.point, query.searchRadius) ? candidate : query.point;
            });
            d.registerQueryHandler<FindPathQuery>([this](const FindPathQuery& query) -> navigation::NavPath
            {
                pathQueries.push_back(query);
                navigation::NavPath path;
                if (!mesh || !connected)
                {
                    return path;
                }
                path.isValid = true;
                path.waypoints.push_back(query.start);
                if (wallX.has_value() && (query.start.x < *wallX) != (query.end.x < *wallX))
                {
                    const float stop = query.start.x < *wallX ? *wallX - 1.0f : *wallX + 1.0f;
                    path.waypoints.emplace_back(stop, 0.0f, query.start.z);
                    path.isPartial = !hierarchical;
                    return path;
                }
                path.waypoints.push_back(query.end);
                return path;
            });

            d.registerCommandHandler<RequestNavmeshBakeCommand>([this](const RequestNavmeshBakeCommand& command)
            {
                NavmeshBakeTicket result;
                if (refuseBake)
                {
                    result.error = refusal;
                    return result;
                }
                requests.push_back(command);
                settings = command.settings;
                ticket = requests.size();
                polls = 0;
                running = true;
                result.accepted = true;
                result.ticket = ticket;
                return result;
            });
            d.registerQueryHandler<GetNavmeshBakeOutcomeQuery>([this](const GetNavmeshBakeOutcomeQuery& query)
            {
                ++outcomePolls;
                NavmeshBakeOutcome outcome;
                outcome.ticket = query.ticket;
                if (query.ticket == 0 || query.ticket != ticket)
                {
                    return outcome;
                }
                if (running && polls++ < runningPolls)
                {
                    outcome.state = NavmeshBakeState::Running;
                    outcome.progress = 0.5f;
                    outcome.stage = "Building";
                    return outcome;
                }
                if (running)
                {
                    running = false;
                    if (finalState == NavmeshBakeState::Succeeded)
                    {
                        mesh = true;
                        stats = bakedStats;
                        unsavedChanges = true;
                    }
                }
                outcome.state = finalState;
                outcome.progress = 1.0f;
                if (finalState == NavmeshBakeState::Succeeded)
                {
                    outcome.stats = bakedStats;
                }
                else
                {
                    outcome.message = failure;
                }
                return outcome;
            });
            d.registerQueryHandler<GetNavmeshStatusQuery>([this](const GetNavmeshStatusQuery&)
            {
                NavmeshStatus status;
                status.hasNavmesh = mesh;
                status.bakeRunning = running || externalBake;
                status.stats = mesh ? stats : navigation::NavmeshStats{};
                status.settings = settings;
                status.indexPath = indexPath;
                status.unsavedChanges = unsavedChanges;
                return status;
            });
            d.registerQueryHandler<GetNavmeshSettingsQuery>([this](const GetNavmeshSettingsQuery&) { return settings; });
            d.registerQueryHandler<GetBakeProgressQuery>([](const GetBakeProgressQuery&)
            {
                types::NavmeshBakeProgress progress;
                progress.status = types::NavmeshBakeStatus::Building;
                progress.progress = 0.5f;
                progress.currentStage = "Building";
                return progress;
            });
            d.registerCommandHandler<SaveNavmeshTiledCommand>([this](const SaveNavmeshTiledCommand& command)
            {
                savedDirectories.push_back(command.directory);
                if (saveResult)
                {
                    indexPath = command.directory + "/index.vfNavIndex";
                    unsavedChanges = false;
                }
                return saveResult;
            });
            d.registerCommandHandler<ClearNavmeshCommand>([this](const ClearNavmeshCommand&)
            {
                ++clears;
                mesh = false;
                indexPath.clear();
                unsavedChanges = false;
            });
            d.registerQueryHandler<GetNavmeshDebugMeshQuery>([](const GetNavmeshDebugMeshQuery&)
            {
                NavmeshDebugMeshResult result;
                result.vertices = {glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f)};
                result.indices = {0, 1, 2};
                return result;
            });
            d.registerCommandHandler<events::render::SetShowNavmeshDebugCommand>(
                [this](const events::render::SetShowNavmeshDebugCommand& command) { overlay = command.show; });
            d.registerCommandHandler<events::render::UpdateNavmeshDebugMeshCommand>(
                [this](const events::render::UpdateNavmeshDebugMeshCommand&) { ++overlayUploads; });
            d.registerCommandHandler<RemoveAgentCommand>([this](const RemoveAgentCommand& command)
            {
                crowd.erase(command.entity.id);
            });

            d.registerQueryHandler<events::scene::GetNavmeshAgentDataQuery>(
                [this](const events::scene::GetNavmeshAgentDataQuery& query) -> std::optional<services::NavmeshAgentData>
                {
                    const entt::entity entity = static_cast<entt::entity>(static_cast<uint32_t>(query.entity.id));
                    if (!reg().valid(entity) || !reg().all_of<components::NavmeshAgentComponent>(entity))
                    {
                        return std::nullopt;
                    }
                    const auto& agent = reg().get<components::NavmeshAgentComponent>(entity);
                    services::NavmeshAgentData data;
                    data.radius = agent.radius;
                    data.height = agent.height;
                    data.baseOffset = agent.baseOffset;
                    data.maxSpeed = agent.maxSpeed;
                    data.maxAcceleration = agent.maxAcceleration;
                    data.turnSpeed = agent.turnSpeed;
                    data.stoppingDistance = agent.stoppingDistance;
                    data.avoidanceQuality = agent.avoidanceQuality;
                    data.rootMotionDriven = agent.rootMotionDriven;
                    data.rootMotionSpeedScale = agent.rootMotionSpeedScale;
                    data.inCrowd = crowd.contains(query.entity.id);
                    return data;
                });
            d.registerCommandHandler<events::scene::SetNavmeshAgentDataCommand>(
                [](const events::scene::SetNavmeshAgentDataCommand& command)
                {
                    const entt::entity entity = static_cast<entt::entity>(static_cast<uint32_t>(command.entity.id));
                    if (!usable(command.data) || !reg().valid(entity) ||
                        !reg().all_of<components::NavmeshAgentComponent>(entity))
                    {
                        return false;
                    }
                    auto& agent = reg().get<components::NavmeshAgentComponent>(entity);
                    agent.radius = command.data.radius;
                    agent.height = command.data.height;
                    agent.baseOffset = command.data.baseOffset;
                    agent.maxSpeed = command.data.maxSpeed;
                    agent.maxAcceleration = command.data.maxAcceleration;
                    agent.turnSpeed = command.data.turnSpeed;
                    agent.stoppingDistance = command.data.stoppingDistance;
                    agent.avoidanceQuality = static_cast<uint8_t>(command.data.avoidanceQuality);
                    agent.rootMotionDriven = command.data.rootMotionDriven;
                    agent.rootMotionSpeedScale = command.data.rootMotionSpeedScale;
                    return true;
                });
        }
    };

    struct NavToolFixture
    {
        DispatcherScope dispatcherScope;
        fs::path project;
        std::shared_ptr<scene::SceneGraphSystem> sceneGraph;
        services::EntityStateService entityState;
        services::HierarchyService hierarchy;
        services::EntityQueryService entityQuery;
        services::TransformComponentService transforms;
        services::ScenePersistenceService persistence;
        services::PhysicsComponentService physics;
        mcp::MainThreadQueue queue;
        mcp::ToolRegistry registry;
        FakeNavmesh navmesh;

        bool playMode = false;
        bool loadPending = false;
        bool worldMode = false;
        std::string scenePath;
        std::vector<std::shared_ptr<services::IUndoableCommand>> undoStack;
        // Behavior tree state the route followers come from, keyed by entity id.
        std::map<uint64_t, services::BehaviorTreeComponentData> treeData;
        std::map<uint64_t, behaviortree::BTRuntimeState> runtimeStates;

        explicit NavToolFixture(const char* directory)
            : project(makeTestDirectory(directory))
            , sceneGraph(std::make_shared<scene::SceneGraphSystem>())
            , entityState(sceneGraph)
            , hierarchy(sceneGraph)
            , entityQuery(sceneGraph)
            , transforms(sceneGraph)
            , persistence(sceneGraph, &entityState)
            , physics(sceneGraph)
        {
            auto& d = events::EventDispatcher::instance();
            entityState.registerEventHandlers(d);
            hierarchy.registerEventHandlers(d);
            entityQuery.registerEventHandlers(d);
            transforms.registerEventHandlers(d);
            persistence.registerEventHandlers(d);
            physics.registerEventHandlers(d);
            navmesh.registerHandlers();

            // Editor state. IsSceneLoadPendingQuery replaces the persistence service's own handler.
            d.unregisterQueryHandler<events::scene::IsSceneLoadPendingQuery>();
            d.registerQueryHandler<events::editor::IsPlayModeQuery>(
                [this](const events::editor::IsPlayModeQuery&) { return playMode; });
            d.registerQueryHandler<events::scene::IsSceneLoadPendingQuery>(
                [this](const events::scene::IsSceneLoadPendingQuery&) { return loadPending; });
            d.registerQueryHandler<events::world::IsWorldModeQuery>(
                [this](const events::world::IsWorldModeQuery&) { return worldMode; });
            d.registerQueryHandler<events::scene::GetCurrentScenePathQuery>(
                [this](const events::scene::GetCurrentScenePathQuery&) { return scenePath; });
            d.registerQueryHandler<events::project::GetCurrentProjectQuery>(
                [this](const events::project::GetCurrentProjectQuery&) -> std::optional<config::ProjectConfig>
                {
                    config::ProjectConfig config;
                    config.projectName = "McpNav";
                    config.workingDirectory = project.string();
                    return config;
                });
            d.registerCommandHandler<events::undoredo::PushUndoableCommand>(
                [this](const events::undoredo::PushUndoableCommand& command) { undoStack.push_back(command.command); });
            d.registerQueryHandler<events::ai::GetBehaviorTreeComponentDataQuery>(
                [this](const events::ai::GetBehaviorTreeComponentDataQuery& query)
                    -> std::optional<services::BehaviorTreeComponentData>
                {
                    const auto it = treeData.find(query.entity.id);
                    if (it == treeData.end())
                    {
                        return std::nullopt;
                    }
                    return it->second;
                });
            d.registerQueryHandler<events::ai::GetBehaviorTreeRuntimeStateQuery>(
                [this](const events::ai::GetBehaviorTreeRuntimeStateQuery& query)
                {
                    const auto it = runtimeStates.find(query.entity.id);
                    return it == runtimeStates.end() ? behaviortree::BTRuntimeState{} : it->second;
                });

            const mcp::tools::ToolContext context{queue};
            mcp::tools::registerNavigationTools(registry, context);
            mcp::tools::registerComponentTools(registry, context);
        }

        ~NavToolFixture()
        {
            // Handlers first (they capture the services), then the entities this scene graph made.
            events::EventDispatcher::instance().clear();
            sceneGraph->clearScene();
            reg().destroy(sceneGraph->GetRoot().getHandle());
            std::error_code ec;
            fs::remove_all(project, ec);
        }

        NavToolFixture(const NavToolFixture&) = delete;
        NavToolFixture& operator=(const NavToolFixture&) = delete;

        const mcp::ToolDef& tool(const char* name) const
        {
            const std::shared_ptr<const mcp::ToolDef> found = registry.find(name);
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

        // The failure of a call, whether reported in-band (isError) or thrown.
        std::string failure(const char* name, const json& args)
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

        uint32_t makeEntity(const std::string& name, std::optional<uint32_t> parent = std::nullopt)
        {
            events::scene::CreateEntityCommand create;
            create.name = name;
            if (parent.has_value())
            {
                create.parent = handleOf(*parent);
            }
            const services::EntityHandle entity = events::EventDispatcher::instance().execute(create);
            REQUIRE(entity.isValid());
            return static_cast<uint32_t>(entity.id);
        }

        void place(uint32_t id, const glm::vec3& position)
        {
            events::scene::SetTransformCommand command;
            command.entity = handleOf(id);
            command.transform.position = position;
            events::EventDispatcher::instance().execute(command);
        }

        uint32_t rootId() const
        {
            return static_cast<uint32_t>(sceneGraph->GetRoot().getHandle());
        }

        std::size_t rootChildCount() const
        {
            const auto* children = reg().try_get<components::ChildrenComponent>(sceneGraph->GetRoot().getHandle());
            return children != nullptr ? children->children.size() : 0;
        }

        std::vector<entt::entity> childrenOf(uint32_t id) const
        {
            const auto* children = reg().try_get<components::ChildrenComponent>(enttOf(id));
            return children != nullptr ? children->children : std::vector<entt::entity>{};
        }

        static uint32_t live(uint32_t id)
        {
            return mcp::undo::EntityIdRemap::instance().resolve(id);
        }

        void undoLast()
        {
            REQUIRE_FALSE(undoStack.empty());
            undoStack.back()->undo();
        }

        void redoLast()
        {
            REQUIRE_FALSE(undoStack.empty());
            undoStack.back()->execute();
        }

        uint32_t makeRoute(const char* name, const json& points)
        {
            return callOk("route_create", {{"name", name}, {"points", points}})["route"].get<uint32_t>();
        }

        json agentData(uint32_t entity)
        {
            return callOk("component_get", {{"entity", entity}, {"type", "NavmeshAgent"}})["data"];
        }
    };

    json square()
    {
        return json::array({point(0, 0, 0), point(10, 0, 0), point(10, 0, 10)});
    }
}

TEST_SUITE("McpNavigationTools")
{
    TEST_CASE("navigation tools: registration, affinity and flags")
    {
        NavToolFixture fixture("vf_mcp_nav_registration");
        for (const char* name : {"navmesh_bake", "navmesh_get_info", "navmesh_clear", "navmesh_find_path",
                                 "route_create", "route_set", "route_get"})
        {
            CAPTURE(name);
            REQUIRE(fixture.registry.find(name).get() != nullptr);
            CHECK(fixture.tool(name).inputSchema.at("additionalProperties") == false);
        }

        const mcp::ToolDef& bake = fixture.tool("navmesh_bake");
        CHECK(bake.affinity == mcp::ThreadAffinity::Worker);
        CHECK(bake.destructive);
        CHECK(bake.timeout.count() >= 300000);
        for (const char* name : {"navmesh_get_info", "navmesh_clear", "navmesh_find_path", "route_create", "route_set",
                                 "route_get"})
        {
            CAPTURE(name);
            CHECK(fixture.tool(name).affinity == mcp::ThreadAffinity::Main);
        }
        CHECK(fixture.tool("navmesh_get_info").readOnly);
        CHECK(fixture.tool("navmesh_find_path").readOnly);
        CHECK(fixture.tool("route_get").readOnly);
        CHECK(fixture.tool("navmesh_clear").destructive);
        CHECK_FALSE(fixture.tool("route_create").readOnly);
        CHECK_FALSE(fixture.tool("route_set").readOnly);
    }

    TEST_CASE("navmesh_bake polls its ticket, saves into the default folder and turns the overlay on")
    {
        NavToolFixture fixture("vf_mcp_nav_bake");
        fixture.navmesh.runningPolls = 3;

        const mcp::ToolResult result =
            fixture.callWorker("navmesh_bake", {{"agentRadius", 0.4}, {"include", {{"foliage", false}}}});
        INFO(result.text);
        REQUIRE_FALSE(result.isError);

        // Keys not given keep the last bake's settings.
        REQUIRE(fixture.navmesh.requests.size() == 1);
        const events::navmesh::RequestNavmeshBakeCommand& request = fixture.navmesh.requests.front();
        const types::NavmeshBakeSettings defaults;
        CHECK(request.settings.agentRadius == 0.4f);
        CHECK(request.settings.agentHeight == defaults.agentHeight);
        CHECK(request.settings.cellSize == defaults.cellSize);
        CHECK_FALSE(request.settings.includeFoliage);
        CHECK(request.settings.includeTerrain);
        CHECK(request.settings.includeStaticMeshes);
        CHECK_FALSE(request.bounds.has_value());
        CHECK(fixture.navmesh.outcomePolls == 4);  // three Running answers, then Succeeded

        // The scene was never saved: navmesh/untitled in the assets folder (the working directory), with a warning.
        REQUIRE(fixture.navmesh.savedDirectories.size() == 1);
        CHECK(fixture.navmesh.savedDirectories.front() == (fixture.project / "navmesh" / "untitled").generic_string());
        const json& out = result.structured;
        CHECK(out.at("saved").at("directory") == "navmesh/untitled");
        CHECK(out.at("saved").at("index") == "navmesh/untitled/index.vfNavIndex");
        CHECK(anyContains(out.at("warnings"), "navmesh/untitled"));
        CHECK(out.at("tiles") == 4);
        CHECK(out.at("polygons") == 120);
        CHECK(out.at("vertices") == 300);
        CHECK(out.at("bounds").at("min") == point(-64, 0, -64));
        CHECK(out.at("settings").at("agentRadius") == 0.4);
        CHECK(out.at("settings").at("include").at("foliage") == false);
        CHECK(out.at("durationMs").is_number_integer());

        CHECK(out.at("overlay") == true);
        CHECK(fixture.navmesh.overlay);
        CHECK(fixture.navmesh.overlayUploads == 1);
        CHECK_FALSE(fixture.navmesh.unsavedChanges);
        CHECK(fixture.undoStack.empty());  // not undoable

        // showDebug false leaves the overlay alone.
        fixture.navmesh.overlay = false;
        const mcp::ToolResult quiet = fixture.callWorker("navmesh_bake", {{"showDebug", false}});
        REQUIRE_FALSE(quiet.isError);
        CHECK(quiet.structured.at("overlay") == false);
        CHECK_FALSE(fixture.navmesh.overlay);
        // The second bake keeps the first one's agentRadius.
        CHECK(fixture.navmesh.requests.back().settings.agentRadius == 0.4f);
    }

    TEST_CASE("navmesh_bake saves where the scene's navmesh lives, else under the scene's name, else into 'directory'")
    {
        NavToolFixture fixture("vf_mcp_nav_bake_folder");
        fixture.scenePath = (fixture.project / "scenes" / "Level.vfScene").string();

        // The folder of the navmesh the scene already references.
        fixture.navmesh.indexPath = (fixture.project / "levels" / "navA" / "index.vfNavIndex").generic_string();
        mcp::ToolResult result = fixture.callWorker("navmesh_bake", json::object());
        INFO(result.text);
        REQUIRE_FALSE(result.isError);
        CHECK(fixture.navmesh.savedDirectories.back() == (fixture.project / "levels" / "navA").generic_string());
        CHECK(result.structured.at("saved").at("directory") == "levels/navA");

        // No navmesh yet: navmesh/<scene file name>, no warning about the folder.
        fixture.navmesh.indexPath.clear();
        result = fixture.callWorker("navmesh_bake", json::object());
        REQUIRE_FALSE(result.isError);
        CHECK(fixture.navmesh.savedDirectories.back() == (fixture.project / "navmesh" / "Level").generic_string());
        CHECK(result.structured.at("saved").at("directory") == "navmesh/Level");
        CHECK_FALSE(anyContains(result.structured.at("warnings"), "untitled"));

        // 'directory' wins.
        result = fixture.callWorker("navmesh_bake", {{"directory", "nav/custom"}});
        REQUIRE_FALSE(result.isError);
        CHECK(fixture.navmesh.savedDirectories.back() ==
              fs::weakly_canonical(fixture.project / "nav" / "custom").generic_string());
        CHECK(result.structured.at("saved").at("directory") == "nav/custom");
        CHECK(result.structured.at("saved").at("index") == "nav/custom/index.vfNavIndex");

        // A folder outside the project is refused before anything is baked.
        const std::size_t requests = fixture.navmesh.requests.size();
        CHECK_THROWS_AS(fixture.callWorker("navmesh_bake", {{"directory", "../outside"}}), mcp::ArgError);
        CHECK(fixture.navmesh.requests.size() == requests);
    }

    TEST_CASE("navmesh_bake refuses Play mode, World mode, a pending scene load and a running bake")
    {
        NavToolFixture fixture("vf_mcp_nav_bake_refusals");

        fixture.playMode = true;
        mcp::ToolResult result = fixture.callWorker("navmesh_bake", json::object());
        CHECK(result.isError);
        CHECK(contains(result.text, "Play mode"));
        fixture.playMode = false;

        fixture.worldMode = true;
        result = fixture.callWorker("navmesh_bake", json::object());
        CHECK(result.isError);
        CHECK(contains(result.text, "World mode"));
        CHECK(contains(result.text, "Navigation window"));
        fixture.worldMode = false;

        fixture.loadPending = true;
        result = fixture.callWorker("navmesh_bake", json::object());
        CHECK(result.isError);
        CHECK(contains(result.text, "scene load"));
        fixture.loadPending = false;

        fixture.navmesh.externalBake = true;
        result = fixture.callWorker("navmesh_bake", json::object());
        CHECK(result.isError);
        CHECK(contains(result.text, "already running"));

        CHECK(fixture.navmesh.requests.empty());
        CHECK(fixture.navmesh.savedDirectories.empty());
    }

    TEST_CASE("navmesh_bake reports a refused bake, a failed bake and zero polygons, and saves nothing")
    {
        NavToolFixture fixture("vf_mcp_nav_bake_failures");

        fixture.navmesh.refuseBake = true;
        mcp::ToolResult result = fixture.callWorker("navmesh_bake", json::object());
        CHECK(result.isError);
        CHECK(contains(result.text, "refused: No geometry found in scene"));
        CHECK(contains(result.text, "staticMeshes"));
        fixture.navmesh.refuseBake = false;

        fixture.navmesh.finalState = events::navmesh::NavmeshBakeState::Failed;
        result = fixture.callWorker("navmesh_bake", json::object());
        CHECK(result.isError);
        CHECK(contains(result.text, "failed: Rasterization failed"));
        fixture.navmesh.finalState = events::navmesh::NavmeshBakeState::Succeeded;

        fixture.navmesh.bakedStats.polyCount = 0;
        result = fixture.callWorker("navmesh_bake", json::object());
        CHECK(result.isError);
        CHECK(contains(result.text, "no walkable polygons"));
        CHECK(contains(result.text, "maxSlope"));
        CHECK(fixture.navmesh.savedDirectories.empty());

        fixture.navmesh.bakedStats.polyCount = 50;
        fixture.navmesh.saveResult = false;
        result = fixture.callWorker("navmesh_bake", json::object());
        CHECK(result.isError);
        CHECK(contains(result.text, "NOT saved"));
        CHECK(fixture.navmesh.savedDirectories.size() == 1);
    }

    TEST_CASE("navmesh_bake checks every argument before a bake starts")
    {
        NavToolFixture fixture("vf_mcp_nav_bake_args");

        CHECK_THROWS_AS(fixture.callWorker("navmesh_bake", {{"agentRadius", 7.0}}), mcp::ArgError);
        CHECK_THROWS_AS(fixture.callWorker("navmesh_bake", {{"cellSize", 0.01}}), mcp::ArgError);
        CHECK_THROWS_AS(fixture.callWorker("navmesh_bake", {{"maxSlope", "steep"}}), mcp::ArgError);
        CHECK_THROWS_AS(fixture.callWorker("navmesh_bake", {{"bogus", 1}}), mcp::ArgError);
        CHECK_THROWS_AS(fixture.callWorker("navmesh_bake", {{"include", {{"roads", true}}}}), mcp::ArgError);
        CHECK_THROWS_AS(fixture.callWorker("navmesh_bake", {{"showDebug", "yes"}}), mcp::ArgError);
        CHECK_THROWS_AS(fixture.callWorker("navmesh_bake", {{"bounds", {{"min", point(0, 0, 0)}}}}), mcp::ArgError);
        CHECK_THROWS_AS(fixture.callWorker("navmesh_bake",
                                           {{"bounds", {{"min", point(0, 0, 0)}, {"max", point(0, 5, 10)}}}}),
                        mcp::ArgError);
        CHECK(fixture.navmesh.requests.empty());

        // bounds reach the request, with a note about whole tiles; a stepHeight below cellHeight warns.
        const mcp::ToolResult result = fixture.callWorker(
            "navmesh_bake", {{"bounds", {{"min", point(-10, -1, -10)}, {"max", point(10, 5, 10)}}},
                             {"stepHeight", 0.1}, {"cellHeight", 0.2}});
        INFO(result.text);
        REQUIRE_FALSE(result.isError);
        REQUIRE(fixture.navmesh.requests.size() == 1);
        const events::navmesh::RequestNavmeshBakeCommand& request = fixture.navmesh.requests.front();
        REQUIRE(request.bounds.has_value());
        CHECK(request.bounds->min == glm::vec3(-10.0f, -1.0f, -10.0f));
        CHECK(request.bounds->max == glm::vec3(10.0f, 5.0f, 10.0f));
        CHECK(request.settings.agentMaxClimb == 0.1f);
        CHECK(request.settings.cellHeight == 0.2f);
        CHECK(anyContains(result.structured.at("warnings"), "whole tiles"));
        CHECK(anyContains(result.structured.at("warnings"), "stepHeight 0.1 is below cellHeight 0.2"));
    }

    TEST_CASE("navmesh_get_info describes the navmesh, the agents and the routes; navmesh_clear drops it")
    {
        NavToolFixture fixture("vf_mcp_nav_info");
        fixture.navmesh.mesh = true;
        fixture.navmesh.stats = fixture.navmesh.bakedStats;
        fixture.navmesh.indexPath = (fixture.project / "navmesh" / "a" / "index.vfNavIndex").generic_string();
        fixture.navmesh.unsavedChanges = true;

        const uint32_t agent = fixture.makeEntity("Agent");
        fixture.place(agent, glm::vec3(5.0f, 1.0f, 5.0f));
        fixture.callOk("component_add", {{"entity", agent}, {"type", "NavmeshAgent"}, {"data", {{"baseOffset", 1.0}}}});
        const uint32_t route = fixture.makeRoute("Patrol", json::array({point(0, 0, 0), point(5, 0, 0)}));
        const std::size_t undoEntries = fixture.undoStack.size();

        json info = fixture.callOk("navmesh_get_info", json::object());
        CHECK(info.at("hasNavmesh") == true);
        CHECK(info.at("bake").at("state") == "idle");
        CHECK(info.at("stats").at("polygons") == 120);
        CHECK(info.at("settings").at("agentRadius") == 0.25);
        CHECK(info.at("asset").at("index") == "navmesh/a/index.vfNavIndex");
        CHECK(info.at("asset").at("unsavedChanges") == true);
        CHECK(info.at("worldMode") == false);
        REQUIRE(info.at("agents").size() == 1);
        const json& reported = info.at("agents").front();
        CHECK(reported.at("entity") == agent);
        CHECK(reported.at("feet") == point(5, 0, 5));
        CHECK(reported.at("onNavmesh") == true);
        CHECK(reported.at("offset") == 0.0);
        CHECK(reported.at("warnings").empty());
        REQUIRE(info.at("routes").size() == 1);
        CHECK(info.at("routes").front().at("entity") == route);
        CHECK(info.at("routes").front().at("markers") == 2);

        fixture.worldMode = true;
        fixture.navmesh.externalBake = true;
        info = fixture.callOk("navmesh_get_info", json::object());
        CHECK(info.at("worldMode") == true);
        CHECK(info.at("bake").at("state") == "running");
        CHECK(info.at("bake").at("stage") == "Building");
        fixture.worldMode = false;

        // navmesh_clear: refused in Play mode and while a bake runs, then the files are reported kept.
        CHECK(contains(fixture.failure("navmesh_clear", json::object()), "bake is running"));
        fixture.navmesh.externalBake = false;
        fixture.playMode = true;
        CHECK(contains(fixture.failure("navmesh_clear", json::object()), "Play mode"));
        fixture.playMode = false;
        CHECK(fixture.navmesh.clears == 0);

        const json cleared = fixture.callOk("navmesh_clear", json::object());
        CHECK(cleared.at("cleared") == true);
        CHECK(cleared.at("hadNavmesh") == true);
        CHECK(cleared.at("keptFiles") == "navmesh/a/index.vfNavIndex");
        CHECK(fixture.navmesh.clears == 1);
        CHECK_FALSE(fixture.navmesh.mesh);
        CHECK(fixture.undoStack.size() == undoEntries);  // not undoable
        CHECK(fixture.callOk("navmesh_get_info", json::object()).at("hasNavmesh") == false);
    }

    TEST_CASE("navmesh_find_path finds paths and explains every failure")
    {
        NavToolFixture fixture("vf_mcp_nav_find_path");

        // No navmesh.
        json out = fixture.callOk("navmesh_find_path", {{"start", point(0, 0, 0)}, {"end", point(10, 0, 0)}});
        CHECK(out.at("found") == false);
        CHECK(contains(out.at("reason").get<std::string>(), "no navmesh"));
        fixture.navmesh.mesh = true;

        // A complete path; agentRadius / agentHeight default to the bake's.
        out = fixture.callOk("navmesh_find_path", {{"start", point(0, 0, 0)}, {"end", point(10, 0, 0)}});
        CHECK(out.at("found") == true);
        CHECK(out.at("partial") == false);
        CHECK(out.at("reason").is_null());
        CHECK(out.at("path") == json::array({point(0, 0, 0), point(10, 0, 0)}));
        CHECK(out.at("length") == 10.0);
        CHECK(out.at("start").at("onNavmesh") == true);
        CHECK(out.at("start").at("offset") == 0.0);
        REQUIRE_FALSE(fixture.navmesh.pathQueries.empty());
        CHECK(fixture.navmesh.pathQueries.back().agentRadius == types::NavmeshBakeSettings{}.agentRadius);
        CHECK(fixture.navmesh.pathQueries.back().agentHeight == types::NavmeshBakeSettings{}.agentHeight);

        fixture.callOk("navmesh_find_path",
                       {{"start", point(0, 0, 0)}, {"end", point(10, 0, 0)}, {"agentRadius", 0.6}, {"agentHeight", 1.5}});
        CHECK(fixture.navmesh.pathQueries.back().agentRadius == 0.6f);
        CHECK(fixture.navmesh.pathQueries.back().agentHeight == 1.5f);

        // An end off the navmesh: nothing within snapRadius (GetClosestPoint echoed the point).
        out = fixture.callOk("navmesh_find_path", {{"start", point(200, 0, 0)}, {"end", point(10, 0, 0)}});
        CHECK(out.at("found") == false);
        CHECK(out.at("partial") == false);
        CHECK(contains(out.at("reason").get<std::string>(), "the start is off the navmesh"));
        CHECK(out.at("start").at("nearest").is_null());
        CHECK(out.at("start").at("onNavmesh") == false);
        out = fixture.callOk("navmesh_find_path", {{"start", point(0, 0, 0)}, {"end", point(0, 30, 0)}});
        CHECK(contains(out.at("reason").get<std::string>(), "the end is off the navmesh"));

        // Within a wider snapRadius it snaps, with a warning.
        out = fixture.callOk("navmesh_find_path",
                             {{"start", point(60, 0, 0)}, {"end", point(10, 0, 0)}, {"snapRadius", 20.0}});
        CHECK(out.at("found") == true);
        CHECK(out.at("start").at("nearest") == point(50, 0, 0));
        CHECK(out.at("start").at("offset") == 10.0);
        CHECK(anyContains(out.at("warnings"), "start was 10 m off the navmesh"));
        CHECK(fixture.navmesh.pathQueries.back().start == glm::vec3(50.0f, 0.0f, 0.0f));
        CHECK_THROWS_AS(fixture.call("navmesh_find_path",
                                     {{"start", point(0, 0, 0)}, {"end", point(1, 0, 0)}, {"snapRadius", 30.0}}),
                        mcp::ArgError);

        // No connection.
        fixture.navmesh.connected = false;
        out = fixture.callOk("navmesh_find_path", {{"start", point(0, 0, 0)}, {"end", point(10, 0, 0)}});
        CHECK(out.at("found") == false);
        CHECK(contains(out.at("reason").get<std::string>(), "no path connects them"));
        fixture.navmesh.connected = true;

        // Partial: Detour's flag, then a hierarchical path that ends short without it.
        fixture.navmesh.wallX = 0.0f;
        for (const bool hierarchical : {false, true})
        {
            CAPTURE(hierarchical);
            fixture.navmesh.hierarchical = hierarchical;
            out = fixture.callOk("navmesh_find_path", {{"start", point(-10, 0, 0)}, {"end", point(10, 0, 0)}});
            CHECK(out.at("found") == false);
            CHECK(out.at("partial") == true);
            CHECK(contains(out.at("reason").get<std::string>(), "partial"));
            CHECK(contains(out.at("reason").get<std::string>(), "11 m short"));
            CHECK(out.at("path").back() == point(-1, 0, 0));
        }
        fixture.navmesh.wallX.reset();

        // An entity end: a NavmeshAgent's feet (world position minus baseOffset).
        const uint32_t agent = fixture.makeEntity("Walker");
        fixture.place(agent, glm::vec3(5.0f, 1.0f, 5.0f));
        fixture.callOk("component_add", {{"entity", agent}, {"type", "NavmeshAgent"}, {"data", {{"baseOffset", 1.0}}}});
        const uint32_t goal = fixture.makeEntity("Goal");
        fixture.place(goal, glm::vec3(-5.0f, 0.0f, -5.0f));
        out = fixture.callOk("navmesh_find_path", {{"start", agent}, {"end", goal}});
        CHECK(out.at("found") == true);
        CHECK(out.at("start").at("entity") == agent);
        CHECK(out.at("start").at("name") == "Walker");
        CHECK(out.at("start").at("given") == point(5, 0, 5));
        CHECK(out.at("start").at("baseOffset") == 1.0);
        CHECK(out.at("end").at("given") == point(-5, 0, -5));
        CHECK_FALSE(out.at("end").contains("baseOffset"));

        CHECK_THROWS_AS(fixture.call("navmesh_find_path", {{"start", "here"}, {"end", point(0, 0, 0)}}), mcp::ArgError);
        CHECK_THROWS_AS(fixture.call("navmesh_find_path", {{"start", json::array({0, 0})}, {"end", point(0, 0, 0)}}),
                        mcp::ArgError);
        CHECK_THROWS_AS(fixture.call("navmesh_find_path", {{"start", 4000000}, {"end", point(0, 0, 0)}}),
                        std::runtime_error);
        CHECK_THROWS_AS(fixture.call("navmesh_find_path", {{"end", point(0, 0, 0)}}), mcp::ArgError);
    }

    TEST_CASE("route_create builds the route and its markers as one undo step, snapped onto the navmesh")
    {
        NavToolFixture fixture("vf_mcp_nav_route_create");
        fixture.navmesh.mesh = true;

        const json out = fixture.callOk(
            "route_create", {{"name", "Patrol"}, {"points", json::array({point(0, 5, 0), point(10, 5, 0), point(10, 5, 10)})}});
        const uint32_t route = out.at("route").get<uint32_t>();
        REQUIRE(alive(route));
        CHECK(out.at("name") == "Patrol");
        CHECK(out.at("points") == json::array({point(0, 0, 0), point(10, 0, 0), point(10, 0, 10)}));
        CHECK(out.at("warnings").empty());

        // The container: at the scene root, identity transform, a NavRoute component.
        CHECK(reg().all_of<components::NavRouteComponent>(enttOf(route)));
        CHECK(reg().get<components::ParentComponent>(enttOf(route)).parent == enttOf(fixture.rootId()));
        const components::TransformComponent& transform = reg().get<components::TransformComponent>(enttOf(route));
        CHECK(transform.position == glm::vec3(0.0f));
        CHECK(transform.rotation == glm::vec3(0.0f));
        CHECK(transform.scale == glm::vec3(1.0f));

        // The markers WP0.. in order, at the snapped points.
        const std::vector<entt::entity> markers = fixture.childrenOf(route);
        REQUIRE(markers.size() == 3);
        REQUIRE(out.at("markers").size() == 3);
        for (std::size_t i = 0; i < markers.size(); ++i)
        {
            CAPTURE(i);
            CHECK(nameOf(markers[i]) == "WP" + std::to_string(i));
            CHECK(out.at("markers").at(i) == static_cast<uint32_t>(markers[i]));
        }
        CHECK(localPosition(markers[1]) == glm::vec3(10.0f, 0.0f, 0.0f));
        CHECK(localPosition(markers[2]) == glm::vec3(10.0f, 0.0f, 10.0f));

        // One undo step for the whole route; redo re-creates it with its markers.
        REQUIRE(fixture.undoStack.size() == 1);
        CHECK(batchSize(fixture.undoStack.back()) == 1);
        fixture.undoLast();
        CHECK_FALSE(alive(route));
        fixture.redoLast();
        const uint32_t again = NavToolFixture::live(route);
        REQUIRE(alive(again));
        CHECK(reg().all_of<components::NavRouteComponent>(enttOf(again)));
        const std::vector<entt::entity> restored = fixture.childrenOf(again);
        REQUIRE(restored.size() == 3);
        CHECK(nameOf(restored[2]) == "WP2");
        CHECK(localPosition(restored[2]) == glm::vec3(10.0f, 0.0f, 10.0f));

        // snapToNavmesh false keeps the points; a duplicate name and a point far off the navmesh warn.
        const json air = fixture.callOk("route_create", {{"name", "Patrol"},
                                                         {"points", json::array({point(0, 5, 0), point(10, 5, 0)})},
                                                         {"snapToNavmesh", false}});
        CHECK(air.at("points") == json::array({point(0, 5, 0), point(10, 5, 0)}));
        CHECK(localPosition(fixture.childrenOf(air.at("route").get<uint32_t>()).at(1)) == glm::vec3(10.0f, 5.0f, 0.0f));
        CHECK(anyContains(air.at("warnings"), "named 'Patrol'"));

        const json distant = fixture.callOk(
            "route_create", {{"name", "Far"}, {"points", json::array({point(0, 0, 0), point(200, 0, 0), point(120, 0, 0)})}});
        CHECK(anyContains(distant.at("warnings"), "Point 1 [200.0,0.0,0.0] is more than 100 m from the navmesh"));
        CHECK(anyContains(distant.at("warnings"), "Point 2 moved 70 m sideways onto the navmesh"));
        CHECK(distant.at("points").at(1) == point(200, 0, 0));
        CHECK(distant.at("points").at(2) == point(50, 0, 0));
    }

    TEST_CASE("route_create refusals and bad arguments leave nothing behind")
    {
        NavToolFixture fixture("vf_mcp_nav_route_refusals");
        const std::size_t children = fixture.rootChildCount();

        fixture.playMode = true;
        CHECK(contains(fixture.failure("route_create", {{"name", "R"}, {"points", square()}}), "Play mode"));
        fixture.playMode = false;
        fixture.worldMode = true;
        CHECK(contains(fixture.failure("route_create", {{"name", "R"}, {"points", square()}}), "World mode"));
        fixture.worldMode = false;
        fixture.loadPending = true;
        CHECK(contains(fixture.failure("route_create", {{"name", "R"}, {"points", square()}}), "scene load"));
        fixture.loadPending = false;

        CHECK_THROWS_AS(fixture.call("route_create", {{"name", "R"}, {"points", json::array({point(0, 0, 0)})}}),
                        mcp::ArgError);
        json tooMany = json::array();
        for (int i = 0; i < 257; ++i)
        {
            tooMany.push_back(point(i, 0, 0));
        }
        CHECK_THROWS_AS(fixture.call("route_create", {{"name", "R"}, {"points", tooMany}}), mcp::ArgError);
        CHECK_THROWS_AS(fixture.call("route_create", {{"name", "R"}, {"points", json::array({point(0, 0, 0),
                                                                                             json::array({1, 2})})}}),
                        mcp::ArgError);
        CHECK_THROWS_AS(fixture.call("route_create", {{"name", "R"}, {"points", json::array({point(0, 0, 0),
                                                                                             point(2e6, 0, 0)})}}),
                        mcp::ArgError);
        CHECK_THROWS_AS(fixture.call("route_create", {{"name", ""}, {"points", square()}}), mcp::ArgError);
        CHECK_THROWS_AS(fixture.call("route_create", {{"points", square()}}), mcp::ArgError);
        CHECK_THROWS_AS(fixture.call("route_create", {{"name", "R"}, {"points", square()}, {"loop", true}}),
                        mcp::ArgError);
        CHECK(fixture.rootChildCount() == children);
        CHECK(fixture.undoStack.empty());

        // Without a navmesh the points are kept as given, with a warning.
        const json out = fixture.callOk("route_create", {{"name", "R"}, {"points", json::array({point(0, 3, 0),
                                                                                               point(4, 3, 0)})}});
        CHECK(out.at("points") == json::array({point(0, 3, 0), point(4, 3, 0)}));
        CHECK(anyContains(out.at("warnings"), "not snapped"));
    }

    TEST_CASE("route_set moves, adds, removes and renames as one undo step")
    {
        NavToolFixture fixture("vf_mcp_nav_route_set");
        fixture.navmesh.mesh = true;
        const uint32_t route = fixture.makeRoute("Patrol", square());
        REQUIRE(fixture.undoStack.size() == 1);

        // Grow: WP0 stays, WP1 and WP2 move, WP3 and WP4 are added.
        const json grown = fixture.callOk(
            "route_set", {{"route", route},
                          {"points", json::array({point(0, 0, 0), point(20, 0, 0), point(20, 0, 20), point(0, 0, 20),
                                                  point(5, 0, 5)})}});
        CHECK(grown.at("changed") == true);
        CHECK(grown.at("markers").size() == 5);
        REQUIRE(fixture.undoStack.size() == 2);
        CHECK(batchSize(fixture.undoStack.back()) == 4);
        std::vector<entt::entity> markers = fixture.childrenOf(route);
        REQUIRE(markers.size() == 5);
        CHECK(nameOf(markers[3]) == "WP3");
        CHECK(nameOf(markers[4]) == "WP4");
        CHECK(localPosition(markers[1]) == glm::vec3(20.0f, 0.0f, 0.0f));
        CHECK(localPosition(markers[4]) == glm::vec3(5.0f, 0.0f, 5.0f));

        fixture.undoLast();
        markers = fixture.childrenOf(route);
        REQUIRE(markers.size() == 3);
        CHECK(localPosition(markers[1]) == glm::vec3(10.0f, 0.0f, 0.0f));
        CHECK(localPosition(markers[2]) == glm::vec3(10.0f, 0.0f, 10.0f));
        fixture.redoLast();
        markers = fixture.childrenOf(route);
        REQUIRE(markers.size() == 5);
        CHECK(nameOf(markers[4]) == "WP4");
        CHECK(localPosition(markers[3]) == glm::vec3(0.0f, 0.0f, 20.0f));

        // Shrink: WP0 and WP1 move, the last three are deleted; undo brings them back in order.
        const json shrunk = fixture.callOk("route_set",
                                           {{"route", route}, {"points", json::array({point(1, 0, 1), point(2, 0, 2)})}});
        CHECK(shrunk.at("points") == json::array({point(1, 0, 1), point(2, 0, 2)}));
        REQUIRE(fixture.undoStack.size() == 3);
        CHECK(batchSize(fixture.undoStack.back()) == 5);
        CHECK(fixture.childrenOf(route).size() == 2);
        fixture.undoLast();
        markers = fixture.childrenOf(route);
        REQUIRE(markers.size() == 5);
        for (std::size_t i = 0; i < markers.size(); ++i)
        {
            CAPTURE(i);
            CHECK(nameOf(markers[i]) == "WP" + std::to_string(i));
        }
        CHECK(localPosition(markers[0]) == glm::vec3(0.0f));
        CHECK(localPosition(markers[2]) == glm::vec3(20.0f, 0.0f, 20.0f));
        CHECK(localPosition(markers[4]) == glm::vec3(5.0f, 0.0f, 5.0f));

        // Rename.
        const json renamed = fixture.callOk("route_set", {{"route", route}, {"name", "Renamed"}});
        CHECK(renamed.at("name") == "Renamed");
        CHECK(nameOf(enttOf(route)) == "Renamed");
        fixture.undoLast();
        CHECK(nameOf(enttOf(route)) == "Patrol");

        // Nothing to change: no undo entry.
        const std::size_t entries = fixture.undoStack.size();
        const json same = fixture.callOk("route_set", {{"route", route}, {"name", "Patrol"}});
        CHECK(same.at("changed") == false);
        CHECK(fixture.undoStack.size() == entries);

        // snapToNavmesh alone snaps the current markers again.
        fixture.place(static_cast<uint32_t>(fixture.childrenOf(route).at(1)), glm::vec3(20.0f, 4.0f, 0.0f));
        const json resnapped = fixture.callOk("route_set", {{"route", route}, {"snapToNavmesh", true}});
        CHECK(resnapped.at("changed") == true);
        CHECK(resnapped.at("points").at(1) == point(20, 0, 0));

        // Refusals and bad arguments.
        const uint32_t marker = static_cast<uint32_t>(fixture.childrenOf(route).at(0));
        CHECK(contains(fixture.failure("route_set", {{"route", marker}, {"name", "X"}}), "marker of route"));
        const uint32_t plain = fixture.makeEntity("Plain");
        CHECK(contains(fixture.failure("route_set", {{"route", plain}, {"name", "X"}}), "not a route"));
        CHECK_THROWS_AS(fixture.call("route_set", {{"route", route}}), mcp::ArgError);
        CHECK_THROWS_AS(fixture.call("route_set", {{"route", route}, {"snapToNavmesh", false}}), mcp::ArgError);
        CHECK_THROWS_AS(fixture.call("route_set", {{"route", route}, {"name", ""}}), mcp::ArgError);
        fixture.playMode = true;
        CHECK(contains(fixture.failure("route_set", {{"route", route}, {"name", "X"}}), "Play mode"));
        fixture.playMode = false;
        fixture.worldMode = true;
        CHECK(contains(fixture.failure("route_set", {{"route", route}, {"name", "X"}}), "World mode"));
    }

    TEST_CASE("route_get reports the markers' world points and the followers")
    {
        NavToolFixture fixture("vf_mcp_nav_route_get");
        fixture.navmesh.mesh = true;
        const uint32_t route = fixture.makeRoute("Patrol", json::array({point(0, 0, 0), point(6, 0, 0)}));

        // An enemy whose start values hold the route, and whose live tree reads it from an Int key.
        const uint32_t enemy = fixture.makeEntity("Enemy");
        reg().emplace<components::BehaviorTreeComponent>(enttOf(enemy));
        services::BehaviorTreeComponentData data;
        services::BehaviorTreeInitialValueData start;
        start.key = "route";
        start.value = handleOf(route);
        data.initialValues.push_back(start);
        fixture.treeData[handleOf(enemy).id] = data;

        behaviortree::BTRuntimeState state;
        state.attached = true;
        auto tree = std::make_shared<behaviortree::BehaviorTreeData>();
        behaviortree::BTNode patrol;
        patrol.id = 2;
        patrol.type = behaviortree::BTNodeType::FollowRoute;
        patrol.properties["routeKey"] = std::string("patrolRoute");
        tree->graph.nodes.push_back(patrol);
        state.tree = tree;
        state.snapshot.blackboard.emplace_back("patrolRoute", static_cast<int32_t>(route));
        state.snapshot.blackboard.emplace_back("hits", static_cast<int32_t>(route));  // an Int no FollowRoute reads
        fixture.runtimeStates[handleOf(enemy).id] = state;

        const json one = fixture.callOk("route_get", {{"route", route}});
        CHECK(one.at("entity") == route);
        CHECK(one.at("name") == "Patrol");
        CHECK(one.at("points") == json::array({point(0, 0, 0), point(6, 0, 0)}));
        CHECK(one.at("markerIds").size() == 2);
        CHECK(one.at("warnings").empty());
        const json& followers = one.at("followers");
        REQUIRE(followers.size() == 2);
        CHECK(followers.at(0).at("entity") == enemy);
        CHECK(followers.at(0).at("key") == "route");
        CHECK(followers.at(0).at("source") == "startValue");
        CHECK(followers.at(1).at("key") == "patrolRoute");
        CHECK(followers.at(1).at("source") == "live");

        const json all = fixture.callOk("route_get", json::object());
        CHECK(all.at("count") == 1);
        CHECK(all.at("routes").at(0).at("entity") == route);

        // A moved route reports world points and warns that its local and world positions differ.
        fixture.place(route, glm::vec3(5.0f, 0.0f, 0.0f));
        const json moved = fixture.callOk("route_get", {{"route", route}});
        CHECK(moved.at("points") == json::array({point(5, 0, 0), point(11, 0, 0)}));
        CHECK(anyContains(moved.at("warnings"), "identity transform"));

        // route_set places markers at WORLD points under the moved route.
        fixture.callOk("route_set", {{"route", route}, {"points", json::array({point(5, 0, 0), point(15, 0, 0)})}});
        CHECK(localPosition(fixture.childrenOf(route).at(1)) == glm::vec3(10.0f, 0.0f, 0.0f));

        CHECK(contains(fixture.failure("route_get", {{"route", enemy}}), "not a route"));
        CHECK_THROWS_AS(fixture.call("route_get", {{"routes", route}}), mcp::ArgError);
    }

    TEST_CASE("the NavmeshAgent binding: ranges, read-only fields, stored values and undo through restore")
    {
        NavToolFixture fixture("vf_mcp_nav_agent_binding");
        fixture.navmesh.mesh = true;
        fixture.navmesh.settings.agentRadius = 0.5f;
        fixture.navmesh.settings.agentHeight = 2.0f;
        const uint32_t agent = fixture.makeEntity("Agent");

        const json added = fixture.callOk("component_add", {{"entity", agent}, {"type", "NavmeshAgent"},
                                                             {"data", {{"radius", 0.4}, {"stoppingDistance", 0.3}}}});
        const json& fields = added.at("data");
        CHECK(fields.at("radius") == 0.4);
        CHECK(fields.at("stoppingDistance") == 0.3);
        CHECK(fields.at("avoidanceQuality") == 3);
        CHECK(fields.at("inCrowd") == false);
        CHECK(fields.at("warnings").empty());
        for (const char* key : {"height", "baseOffset", "maxSpeed", "maxAcceleration", "turnSpeed", "rootMotionDriven",
                                "rootMotionSpeedScale"})
        {
            CAPTURE(key);
            CHECK(fields.contains(key));
        }
        REQUIRE(fixture.undoStack.size() == 1);

        // Ranges, read-only and unknown fields.
        const auto set = [&fixture, agent](const json& data)
        {
            return fixture.call("component_set", {{"entity", agent}, {"type", "NavmeshAgent"}, {"data", data}});
        };
        CHECK_THROWS_AS(set({{"radius", 7.0}}), mcp::ArgError);
        CHECK_THROWS_AS(set({{"avoidanceQuality", 4}}), mcp::ArgError);
        CHECK_THROWS_AS(set({{"baseOffset", -6.0}}), mcp::ArgError);
        CHECK_THROWS_AS(set({{"inCrowd", true}}), mcp::ArgError);
        CHECK_THROWS_AS(set({{"warnings", json::array()}}), mcp::ArgError);
        CHECK_THROWS_AS(set({{"separationWeight", 1.0}}), mcp::ArgError);
        CHECK_THROWS_AS(set({{"rootMotionDriven", 1}}), mcp::ArgError);
        CHECK(fixture.undoStack.size() == 1);

        // A value stored outside the range (a scene file) echoes back, and an undo restores it.
        reg().get<components::NavmeshAgentComponent>(enttOf(agent)).radius = 7.0f;
        json data = fixture.agentData(agent);
        CHECK(data.at("radius") == 7.0);
        CHECK(anyContains(data.at("warnings"), "radius 7 is above the navmesh's agentRadius 0.5"));
        data = fixture.callOk("component_set",
                              {{"entity", agent}, {"type", "NavmeshAgent"}, {"data", {{"radius", 7.0}, {"height", 1.5}}}})
                   .at("data");
        CHECK(data.at("radius") == 7.0);
        CHECK(data.at("height") == 1.5);
        fixture.callOk("component_set", {{"entity", agent}, {"type", "NavmeshAgent"}, {"data", {{"radius", 1.0}}}});
        CHECK(reg().get<components::NavmeshAgentComponent>(enttOf(agent)).radius == 1.0f);
        fixture.undoLast();
        CHECK(reg().get<components::NavmeshAgentComponent>(enttOf(agent)).radius == 7.0f);
        fixture.redoLast();
        CHECK(reg().get<components::NavmeshAgentComponent>(enttOf(agent)).radius == 1.0f);

        // In Play mode an edit applies at once.
        fixture.playMode = true;
        CHECK(fixture.callOk("component_set", {{"entity", agent}, {"type", "NavmeshAgent"}, {"data", {{"maxSpeed", 6.0}}}})
                  .at("data").at("maxSpeed") == 6.0);
        fixture.playMode = false;
        fixture.navmesh.crowd.insert(handleOf(agent).id);
        CHECK(fixture.agentData(agent).at("inCrowd") == true);

        // Remove, then undo the removal.
        const std::size_t entries = fixture.undoStack.size();
        fixture.callOk("component_remove", {{"entity", agent}, {"type", "NavmeshAgent"}});
        CHECK_FALSE(reg().all_of<components::NavmeshAgentComponent>(enttOf(agent)));
        REQUIRE(fixture.undoStack.size() == entries + 1);
        fixture.undoLast();
        REQUIRE(reg().all_of<components::NavmeshAgentComponent>(enttOf(agent)));
        CHECK(reg().get<components::NavmeshAgentComponent>(enttOf(agent)).maxSpeed == 6.0f);
    }

    TEST_CASE("the NavmeshAgent binding warns about a parent, the bake's size, root motion and the navmesh")
    {
        NavToolFixture fixture("vf_mcp_nav_agent_warnings");
        fixture.navmesh.settings.agentRadius = 0.5f;
        fixture.navmesh.settings.agentHeight = 2.0f;

        // No navmesh at all.
        const uint32_t agent = fixture.makeEntity("Agent");
        json data = fixture.callOk("component_add", {{"entity", agent}, {"type", "NavmeshAgent"}}).at("data");
        CHECK(anyContains(data.at("warnings"), "No navmesh is baked"));
        fixture.navmesh.mesh = true;
        CHECK(fixture.agentData(agent).at("warnings").empty());

        // A parent other than the scene root.
        const uint32_t group = fixture.makeEntity("Group");
        const uint32_t child = fixture.makeEntity("Child", group);
        data = fixture.callOk("component_add", {{"entity", child}, {"type", "NavmeshAgent"}}).at("data");
        CHECK(anyContains(data.at("warnings"), "Parented"));

        // Feet above the navmesh until baseOffset accounts for the pivot.
        const uint32_t capsule = fixture.makeEntity("Capsule");
        fixture.place(capsule, glm::vec3(3.0f, 1.0f, 3.0f));
        data = fixture.callOk("component_add", {{"entity", capsule}, {"type", "NavmeshAgent"}}).at("data");
        CHECK(anyContains(data.at("warnings"), "Off the navmesh: its feet (world position minus baseOffset) are 1 m"));
        data = fixture.callOk("component_set", {{"entity", capsule}, {"type", "NavmeshAgent"},
                                                {"data", {{"baseOffset", 1.0}}}}).at("data");
        CHECK(data.at("warnings").empty());
        fixture.place(capsule, glm::vec3(300.0f, 1.0f, 3.0f));
        CHECK(anyContains(fixture.agentData(capsule).at("warnings"), "nothing walkable lies within 5 m"));
        fixture.place(capsule, glm::vec3(3.0f, 1.0f, 3.0f));

        // Larger than the bake.
        data = fixture.callOk("component_set", {{"entity", capsule}, {"type", "NavmeshAgent"},
                                                {"data", {{"radius", 0.8}, {"height", 3.0}}}}).at("data");
        CHECK(anyContains(data.at("warnings"), "radius 0.8 is above the navmesh's agentRadius 0.5"));
        CHECK(anyContains(data.at("warnings"), "height 3 is above the navmesh's agentHeight 2"));

        // navmesh_bake lists the agents its settings are too small for.
        const mcp::ToolResult baked = fixture.callWorker("navmesh_bake", {{"agentRadius", 0.5}, {"agentHeight", 2.0}});
        REQUIRE_FALSE(baked.isError);
        CHECK(anyContains(baked.structured.at("warnings"), "NavmeshAgent(s) are larger than this bake"));
        CHECK(anyContains(baked.structured.at("warnings"), "'Capsule' (radius 0.8, height 3)"));

        // rootMotionDriven without root motion (no mesh service registered: no warning).
        data = fixture.callOk("component_set", {{"entity", agent}, {"type", "NavmeshAgent"},
                                                {"data", {{"rootMotionDriven", true}}}}).at("data");
        CHECK_FALSE(anyContains(data.at("warnings"), "rootMotionDriven"));
        events::EventDispatcher::instance().registerQueryHandler<events::scene::GetMeshDataQuery>(
            [](const events::scene::GetMeshDataQuery&) -> std::optional<services::MeshData> { return std::nullopt; });
        CHECK(anyContains(fixture.agentData(agent).at("warnings"), "rootMotionDriven is on"));
    }
}
