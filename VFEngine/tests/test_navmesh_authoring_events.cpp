#include <doctest.h>
#include "impl/navmesh/NavmeshServiceImpl.hpp"
#include "impl/components/PhysicsComponentService.hpp"
#include "impl/scene/ScenePersistenceService.hpp"
#include "providers/navmesh/INavmeshProvider.hpp"
#include "data/EntityConversion.hpp"
#include "events/EventDispatcher.hpp"
#include "events/navmesh/NavmeshEvents.hpp"
#include "events/navmesh/NavmeshAuthoringEvents.hpp"
#include "events/scene/ComponentPhysicsLightEvents.hpp"
#include "events/scene/EntityTransformEvents.hpp"
#include "events/scene/ScenePersistenceEvents.hpp"
#include "events/render/RenderEvents.hpp"
#include "events/render/PostProcessEvents.hpp"
#include "events/render/AtmosphereEvents.hpp"
#include "events/render/CloudEvents.hpp"
#include "events/physics/PhysicsSettingsEvents.hpp"
#include "events/audio/AudioSettingsEvents.hpp"
#include "events/terrain/OceanEvents.hpp"
#include "scene/EntityRegistry.hpp"
#include "scene/SceneGraphSystem.hpp"
#include "components/Components.hpp"
#include "types/NavmeshTypes.hpp"
#include <entt/entt.hpp>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

// ============================================================
// VK-1660: NavmeshServiceImpl tool support - the ticketed bake (RequestNavmeshBakeCommand /
// GetNavmeshBakeOutcomeQuery / GetNavmeshStatusQuery), NavmeshAgent data, crowd bookkeeping across
// bakes and clears, stoppingDistance / baseOffset, NavRoute add/remove, and the navmesh clear on a
// new scene or a disk load (but not on a Play->Stop snapshot restore).
//
// Deterministic whether or not an earlier suite initialized the JobSystem: a bake is harvested only
// by update(), so the tests pump update() until the ticket leaves Running.
// ============================================================

namespace
{
    namespace fs = std::filesystem;
    using events::navmesh::NavmeshBakeState;

    constexpr int kMaxPumpFrames = 5000;
    constexpr float kEps = 1e-4f;

    // Far from the origin, so geometry other suites leave in the global registry stays out of bounded bakes.
    const glm::vec3 kSite{7000.0f, 0.0f, 7000.0f};

    bool vecApprox(const glm::vec3& a, const glm::vec3& b, float eps = kEps)
    {
        return std::abs(a.x - b.x) <= eps && std::abs(a.y - b.y) <= eps && std::abs(a.z - b.z) <= eps;
    }

    // Records what the service asks of Detour. A bake may run on a JobSystem worker, so the state it
    // writes is guarded (the test reads it only after the bake was harvested).
    class FakeNavmeshProvider : public services::INavmeshProvider
    {
    public:
        bool buildResult = true;
        navigation::NavmeshStats builtStats;
        std::atomic<bool> mesh{false};
        int clearCalls = 0;

        int nextAgent = 0;
        std::unordered_map<int, glm::vec3> agentPositions;
        std::vector<int> removedAgents;
        std::vector<int> stoppedAgents;
        std::vector<std::pair<int, glm::vec3>> targets;
        std::vector<std::pair<int, navigation::CrowdAgentConfig>> configs;

        int buildCalls() const
        {
            std::lock_guard lock(mutex);
            return builds;
        }

        navigation::NavmeshInputGeometry lastGeometry() const
        {
            std::lock_guard lock(mutex);
            return geometry;
        }

        bool init() override { return true; }
        void cleanUp() override {}
        bool isInitialized() const override { return true; }

        bool buildNavmesh(const navigation::NavmeshInputGeometry& input, const types::NavmeshBakeSettings&,
                          const navigation::OffMeshConnectionsMap&, const navigation::AreaModifiersMap&) override
        {
            std::lock_guard lock(mutex);
            ++builds;
            geometry = input;
            progress.status = buildResult ? types::NavmeshBakeStatus::Complete : types::NavmeshBakeStatus::Failed;
            progress.progress = buildResult ? 1.0f : 0.0f;
            progress.currentStage = buildResult ? "Complete" : "Rasterization failed";
            mesh = buildResult;
            return buildResult;
        }

        types::NavmeshBakeProgress getBuildProgress() const override
        {
            std::lock_guard lock(mutex);
            return progress;
        }

        bool initTiledNavmesh(const types::NavmeshBakeSettings&, const glm::vec3&, const glm::vec3&) override { return true; }
        navigation::NavmeshTileData buildSingleTile(int, int, const navigation::NavmeshInputGeometry&,
                                                    const types::NavmeshBakeSettings&,
                                                    const navigation::NavmeshOffMeshConnections&,
                                                    const std::vector<navigation::NavmeshAreaModifier>&,
                                                    uint8_t) override { return {}; }
        bool addNavmeshTile(const navigation::NavmeshTileData&) override { return true; }
        bool removeNavmeshTile(int, int) override { return true; }

        std::vector<navigation::NavmeshTileData> serializeNavmesh() const override { return {}; }
        bool deserializeNavmesh(const navigation::NavmeshFileHeader&,
                                const std::vector<navigation::NavmeshTileData>&) override { return true; }
        bool hasNavmesh() const override { return mesh; }
        void clearNavmesh() override
        {
            mesh = false;
            ++clearCalls;
        }
        navigation::NavmeshStats getNavmeshStats() const override
        {
            return mesh ? builtStats : navigation::NavmeshStats{};
        }

        navigation::NavPath findPath(const glm::vec3&, const glm::vec3&, float, float) override { return {}; }
        navigation::NavmeshRaycastResult navmeshRaycast(const glm::vec3&, const glm::vec3&) override { return {}; }
        glm::vec3 getClosestPoint(const glm::vec3& point, float) override { return point; }
        bool isPointOnNavmesh(const glm::vec3&, float) override { return mesh; }

        int addCrowdAgent(const glm::vec3& position, float, float, float, float) override
        {
            const int index = nextAgent++;
            agentPositions[index] = position;
            return index;
        }
        void removeCrowdAgent(int index) override
        {
            removedAgents.push_back(index);
            agentPositions.erase(index);
        }
        void setCrowdAgentTarget(int index, const glm::vec3& target) override { targets.emplace_back(index, target); }
        void stopCrowdAgent(int index) override { stoppedAgents.push_back(index); }
        void updateCrowdAgentParams(int, float, float) override {}
        glm::vec3 getCrowdAgentPosition(int index) const override
        {
            auto it = agentPositions.find(index);
            return it != agentPositions.end() ? it->second : glm::vec3(0.0f);
        }
        glm::vec3 getCrowdAgentVelocity(int) const override { return glm::vec3(0.0f); }
        float getCrowdAgentMaxSpeed(int) const override { return 0.0f; }
        void updateCrowd(float) override {}
        bool overrideCrowdAgentVelocity(int, const glm::vec3&) override { return false; }
        bool updateCrowdAgentConfig(int index, const navigation::CrowdAgentConfig& config) override
        {
            configs.emplace_back(index, config);
            return true;
        }

        void configureCrowdFilter(int, const float*, int) override {}
        void setCrowdAgentFilterType(int, uint8_t) override {}
        void onTileAdded(int, int) override {}
        void onTileRemoved(int, int) override {}
        void getDebugMesh(std::vector<glm::vec3>& vertices, std::vector<uint32_t>& indices) const override
        {
            vertices.clear();
            indices.clear();
        }

    private:
        mutable std::mutex mutex;
        int builds = 0;
        navigation::NavmeshInputGeometry geometry;
        types::NavmeshBakeProgress progress;
    };

    struct DispatcherScope
    {
        DispatcherScope() { events::EventDispatcher::instance().clear(); }
        ~DispatcherScope() { events::EventDispatcher::instance().clear(); }
    };

    types::NavmeshBakeSettings collidersOnly()
    {
        types::NavmeshBakeSettings settings;
        settings.includeTerrain = false;
        settings.includeStaticMeshes = false;
        settings.includeFoliage = false;
        settings.includeColliders = true;
        return settings;
    }

    // A real NavmeshServiceImpl + PhysicsComponentService over a fake provider. Handlers are cleared
    // with the dispatcher; the entities made here are destroyed first.
    struct NavmeshFixture
    {
        DispatcherScope dispatcherScope;
        FakeNavmeshProvider provider;
        services::NavmeshServiceImpl service{&provider};
        services::PhysicsComponentService physics{nullptr};
        std::vector<entt::entity> entities;
        entt::entity root = entt::null;
        std::vector<std::pair<uint64_t, bool>> staticWrites;
        int arrivals = 0;

        NavmeshFixture()
        {
            scene::EntityRegistry::init();
            auto& d = events::EventDispatcher::instance();
            service.registerEventHandlers();
            physics.registerEventHandlers(d);

            d.registerQueryHandler<events::render::GetShowNavmeshDebugQuery>(
                [](const events::render::GetShowNavmeshDebugQuery&) { return false; });
            d.registerCommandHandler<events::render::ClearNavmeshDebugMeshCommand>(
                [](const events::render::ClearNavmeshDebugMeshCommand&) {});
            d.registerCommandHandler<events::scene::SetEntityStaticCommand>(
                [this](const events::scene::SetEntityStaticCommand& cmd)
                {
                    staticWrites.emplace_back(cmd.entity.id, cmd.isStatic);
                    return true;
                });

            root = make(glm::vec3(0.0f));
            d.registerQueryHandler<events::scene::GetRootEntityQuery>(
                [this](const events::scene::GetRootEntityQuery&) { return services::internal::toHandle(root); });
            (void)d.subscribe<events::navmesh::AgentReachedDestinationNotification>(
                [this](const events::navmesh::AgentReachedDestinationNotification&) { ++arrivals; });
        }

        ~NavmeshFixture()
        {
            auto& registry = scene::EntityRegistry::getRegistry();
            for (auto entity : entities)
            {
                if (registry.valid(entity))
                    registry.destroy(entity);
            }
        }

        entt::entity make(const glm::vec3& position)
        {
            auto& registry = scene::EntityRegistry::getRegistry();
            const entt::entity entity = registry.create();
            registry.emplace<components::TransformComponent>(entity).position = position;
            entities.push_back(entity);
            return entity;
        }

        // A 20 x 1 x 20 box collider centred on kSite: spans y -0.5..0.5.
        entt::entity makeGround()
        {
            const entt::entity ground = make(kSite);
            auto& collider = scene::EntityRegistry::getRegistry().emplace<components::ColliderComponent>(ground);
            collider.shape = components::ColliderShape::Box;
            collider.size = glm::vec3(20.0f, 1.0f, 20.0f);
            return ground;
        }

        entt::entity makeAgent(const glm::vec3& position)
        {
            const entt::entity agent = make(position);
            scene::EntityRegistry::getRegistry().emplace<components::NavmeshAgentComponent>(agent);
            return agent;
        }

        components::NavmeshAgentComponent& agentOf(entt::entity entity)
        {
            return scene::EntityRegistry::getRegistry().get<components::NavmeshAgentComponent>(entity);
        }

        static services::EntityHandle handle(entt::entity entity) { return services::internal::toHandle(entity); }

        events::navmesh::NavmeshBakeTicket requestBake(
            const std::optional<events::navmesh::NavmeshBakeBounds>& bounds = std::nullopt,
            const types::NavmeshBakeSettings& settings = collidersOnly())
        {
            events::navmesh::RequestNavmeshBakeCommand request;
            request.settings = settings;
            request.bounds = bounds;
            return events::EventDispatcher::instance().execute(request);
        }

        events::navmesh::NavmeshBakeOutcome outcome(uint64_t ticket)
        {
            events::navmesh::GetNavmeshBakeOutcomeQuery query;
            query.ticket = ticket;
            return events::EventDispatcher::instance().query(query);
        }

        events::navmesh::NavmeshStatus status()
        {
            return events::EventDispatcher::instance().query(events::navmesh::GetNavmeshStatusQuery{});
        }

        events::navmesh::NavmeshBakeOutcome pumpUntilFinished(uint64_t ticket)
        {
            for (int frame = 0; frame < kMaxPumpFrames; ++frame)
            {
                service.update(0.016f, false);
                const auto current = outcome(ticket);
                if (current.state != NavmeshBakeState::Running)
                    return current;
                std::this_thread::yield();
            }
            return outcome(ticket);
        }

        void registerAgent(entt::entity entity)
        {
            events::navmesh::AddAgentCommand add;
            add.entity = handle(entity);
            events::EventDispatcher::instance().execute(add);
        }

        void setDestination(entt::entity entity, const glm::vec3& target)
        {
            events::navmesh::SetAgentDestinationCommand move;
            move.entity = handle(entity);
            move.target = target;
            events::EventDispatcher::instance().execute(move);
        }

        std::optional<services::NavmeshAgentData> agentData(entt::entity entity)
        {
            events::scene::GetNavmeshAgentDataQuery query;
            query.entity = handle(entity);
            return events::EventDispatcher::instance().query(query);
        }

        bool setAgentData(entt::entity entity, const services::NavmeshAgentData& data)
        {
            events::scene::SetNavmeshAgentDataCommand cmd;
            cmd.entity = handle(entity);
            cmd.data = data;
            return events::EventDispatcher::instance().execute(cmd);
        }
    };

    template <typename TCommand>
    void stubCommand()
    {
        events::EventDispatcher::instance().registerCommandHandler<TCommand>(
            [](const TCommand&) -> typename TCommand::ResultType
            {
                if constexpr (std::is_void_v<typename TCommand::ResultType>)
                    return;
                else
                    return typename TCommand::ResultType{};
            });
    }

    // The renderer / physics / audio / ocean commands newScene, a disk load and a snapshot restore issue.
    void stubSceneLoadCommands()
    {
        stubCommand<events::render::RemoveIBLCommand>();
        stubCommand<events::render::SetIBLParamsCommand>();
        stubCommand<events::render::ApplyShadowSettingsCommand>();
        stubCommand<events::ocean::RebuildOceanFromComponentsCommand>();
        stubCommand<events::physics::ApplyPhysicsSettingsCommand>();
        stubCommand<events::audio::ApplyAudioSettingsCommand>();
        stubCommand<events::postprocess::ApplyPostProcessSettingsCommand>();
        stubCommand<events::atmosphere::ApplyAtmosphereSettingsCommand>();
        stubCommand<events::cloud::ApplyCloudSettingsCommand>();
        events::EventDispatcher::instance().registerQueryHandler<events::scene::GetCurrentScenePathQuery>(
            [](const events::scene::GetCurrentScenePathQuery&) { return std::string{}; });
    }
}

TEST_SUITE("NavmeshAuthoringEvents")
{
    TEST_CASE("a ticketed bake runs, succeeds and reports its stats")
    {
        NavmeshFixture f;
        f.makeGround();
        f.provider.builtStats.tileCount = 4;
        f.provider.builtStats.polyCount = 37;
        f.provider.builtStats.vertexCount = 80;

        const auto ticket = f.requestBake();
        REQUIRE(ticket.accepted);
        CHECK(ticket.ticket != 0);
        CHECK(ticket.error.empty());

        // Harvested only by update(), whichever thread built it.
        CHECK(f.outcome(ticket.ticket).state == NavmeshBakeState::Running);
        CHECK(f.status().bakeRunning);

        const auto finished = f.pumpUntilFinished(ticket.ticket);
        REQUIRE(finished.state == NavmeshBakeState::Succeeded);
        CHECK(finished.ticket == ticket.ticket);
        CHECK(finished.message.empty());
        CHECK(finished.stats.tileCount == 4);
        CHECK(finished.stats.polyCount == 37);
        CHECK(finished.stats.vertexCount == 80);
        CHECK(f.provider.buildCalls() == 1);
        CHECK(f.provider.lastGeometry().getTriangleCount() >= 12); // at least the ground box

        const auto status = f.status();
        CHECK(status.hasNavmesh);
        CHECK_FALSE(status.bakeRunning);
        CHECK(status.unsavedChanges);
        CHECK(status.stats.polyCount == 37);
        CHECK(status.indexPath.empty()); // the bake saved nothing
    }

    TEST_CASE("a second bake is refused while one runs; unknown tickets report Unknown")
    {
        NavmeshFixture f;
        f.makeGround();

        const auto first = f.requestBake();
        REQUIRE(first.accepted);

        const auto second = f.requestBake();
        CHECK_FALSE(second.accepted);
        CHECK(second.ticket == 0);
        CHECK(second.error.find("already running") != std::string::npos);

        CHECK(f.outcome(0).state == NavmeshBakeState::Unknown);
        CHECK(f.outcome(first.ticket + 100).state == NavmeshBakeState::Unknown);

        REQUIRE(f.pumpUntilFinished(first.ticket).state == NavmeshBakeState::Succeeded);
        CHECK(f.outcome(first.ticket).state == NavmeshBakeState::Succeeded); // kept after it finished

        const auto third = f.requestBake();
        REQUIRE(third.accepted);
        CHECK(third.ticket > first.ticket);
        CHECK(f.pumpUntilFinished(third.ticket).state == NavmeshBakeState::Succeeded);
    }

    TEST_CASE("a failed build reports Failed with the provider's reason")
    {
        NavmeshFixture f;
        f.makeGround();
        f.provider.buildResult = false;

        const auto ticket = f.requestBake();
        REQUIRE(ticket.accepted);

        const auto finished = f.pumpUntilFinished(ticket.ticket);
        REQUIRE(finished.state == NavmeshBakeState::Failed);
        CHECK(finished.message == "Rasterization failed");
        CHECK_FALSE(f.status().unsavedChanges);
        CHECK_FALSE(f.status().hasNavmesh);
    }

    TEST_CASE("a bake with nothing to rasterize or invalid input is refused at request time")
    {
        NavmeshFixture f;

        events::navmesh::NavmeshBakeBounds empty;
        empty.min = kSite + glm::vec3(2000.0f, -1.0f, 2000.0f);
        empty.max = kSite + glm::vec3(2010.0f, 1.0f, 2010.0f);
        const auto nothing = f.requestBake(empty);
        CHECK_FALSE(nothing.accepted);
        CHECK(nothing.error == "No geometry inside the bake bounds");

        events::navmesh::NavmeshBakeBounds flat;
        flat.min = kSite;
        flat.max = kSite + glm::vec3(5.0f, 0.0f, 5.0f); // no height
        const auto degenerate = f.requestBake(flat);
        CHECK_FALSE(degenerate.accepted);
        CHECK(degenerate.error.find("empty") != std::string::npos);

        events::navmesh::NavmeshBakeBounds infinite;
        infinite.min = glm::vec3(-std::numeric_limits<float>::infinity());
        infinite.max = kSite;
        CHECK_FALSE(f.requestBake(infinite).accepted);

        types::NavmeshBakeSettings broken = collidersOnly();
        broken.cellSize = 0.0f;
        const auto badSettings = f.requestBake(std::nullopt, broken);
        CHECK_FALSE(badSettings.accepted);
        CHECK(badSettings.error.find("cellSize") != std::string::npos);

        CHECK(f.provider.buildCalls() == 0);
        CHECK_FALSE(f.status().bakeRunning);
    }

    TEST_CASE("bounds limit the bake to the box; its Y range is a floor and a ceiling")
    {
        NavmeshFixture f;
        f.makeGround(); // x, z in kSite +- 10, y in -0.5 .. 0.5

        events::navmesh::NavmeshBakeBounds box;
        box.min = kSite + glm::vec3(0.0f, -1.0f, 0.0f);
        box.max = kSite + glm::vec3(5.0f, 0.25f, 5.0f);
        const auto ticket = f.requestBake(box);
        REQUIRE(ticket.accepted);
        REQUIRE(f.pumpUntilFinished(ticket.ticket).state == NavmeshBakeState::Succeeded);

        const auto geometry = f.provider.lastGeometry();
        CHECK(geometry.getTriangleCount() == 12); // the ground box, nothing else near kSite
        CHECK(vecApprox(geometry.boundsMin, kSite + glm::vec3(0.0f, -0.5f, 0.0f)));
        CHECK(vecApprox(geometry.boundsMax, kSite + glm::vec3(5.0f, 0.25f, 5.0f)));
    }

    TEST_CASE("NavmeshAgent data round-trips, rejects unusable values and reaches the crowd agent")
    {
        NavmeshFixture f;
        const entt::entity agent = f.makeAgent(kSite);

        const auto defaults = f.agentData(agent);
        REQUIRE(defaults.has_value());
        const components::NavmeshAgentComponent fresh;
        CHECK(defaults->radius == doctest::Approx(fresh.radius));
        CHECK(defaults->stoppingDistance == doctest::Approx(fresh.stoppingDistance));
        CHECK(defaults->avoidanceQuality == fresh.avoidanceQuality);
        CHECK_FALSE(defaults->inCrowd);

        services::NavmeshAgentData data = *defaults;
        data.radius = 0.6f;
        data.height = 1.8f;
        data.baseOffset = -0.25f; // negative offsets are allowed
        data.maxSpeed = 5.0f;
        data.maxAcceleration = 12.0f;
        data.turnSpeed = 270.0f;
        data.stoppingDistance = 1.5f;
        data.avoidanceQuality = 1;
        data.rootMotionDriven = true;
        data.rootMotionSpeedScale = 1.25f;
        REQUIRE(f.setAgentData(agent, data));

        const auto stored = f.agentData(agent);
        REQUIRE(stored.has_value());
        CHECK(stored->radius == doctest::Approx(0.6f));
        CHECK(stored->height == doctest::Approx(1.8f));
        CHECK(stored->baseOffset == doctest::Approx(-0.25f));
        CHECK(stored->maxSpeed == doctest::Approx(5.0f));
        CHECK(stored->maxAcceleration == doctest::Approx(12.0f));
        CHECK(stored->turnSpeed == doctest::Approx(270.0f));
        CHECK(stored->stoppingDistance == doctest::Approx(1.5f));
        CHECK(stored->avoidanceQuality == 1);
        CHECK(stored->rootMotionDriven);
        CHECK(stored->rootMotionSpeedScale == doctest::Approx(1.25f));

        SUBCASE("unusable values are refused and change nothing")
        {
            auto bad = data;
            bad.radius = 0.0f;
            CHECK_FALSE(f.setAgentData(agent, bad));
            bad = data;
            bad.height = -1.0f;
            CHECK_FALSE(f.setAgentData(agent, bad));
            bad = data;
            bad.avoidanceQuality = 4;
            CHECK_FALSE(f.setAgentData(agent, bad));
            bad = data;
            bad.stoppingDistance = -0.1f;
            CHECK_FALSE(f.setAgentData(agent, bad));
            bad = data;
            bad.maxSpeed = std::numeric_limits<float>::quiet_NaN();
            CHECK_FALSE(f.setAgentData(agent, bad));
            bad = data;
            bad.baseOffset = std::numeric_limits<float>::infinity();
            CHECK_FALSE(f.setAgentData(agent, bad));
            CHECK(f.agentData(agent)->radius == doctest::Approx(0.6f));
        }

        SUBCASE("an agent in the crowd takes the new shape, speed and avoidance at once")
        {
            f.registerAgent(agent);
            REQUIRE(f.agentData(agent)->inCrowd);
            REQUIRE_FALSE(f.provider.configs.empty());
            CHECK(f.provider.configs.back().second.avoidanceQuality == 1); // applied at registration

            data.radius = 0.4f;
            data.maxSpeed = 2.0f;
            data.avoidanceQuality = 2;
            REQUIRE(f.setAgentData(agent, data));
            const auto& pushed = f.provider.configs.back();
            CHECK(pushed.first == f.agentOf(agent).crowdAgentIndex);
            CHECK(pushed.second.radius == doctest::Approx(0.4f));
            CHECK(pushed.second.height == doctest::Approx(1.8f));
            CHECK(pushed.second.maxSpeed == doctest::Approx(2.0f));
            CHECK(pushed.second.maxAcceleration == doctest::Approx(12.0f));
            CHECK(pushed.second.avoidanceQuality == 2);
        }

        SUBCASE("an entity without the component has no data")
        {
            const entt::entity plain = f.make(kSite);
            CHECK_FALSE(f.agentData(plain).has_value());
            CHECK_FALSE(f.setAgentData(plain, data));
        }
    }

    TEST_CASE("registration: the crowd gets the navmesh surface point and the agent turns dynamic")
    {
        NavmeshFixture f;
        const entt::entity agent = f.makeAgent(kSite + glm::vec3(0.0f, 1.0f, 0.0f));
        f.agentOf(agent).baseOffset = 1.0f; // a capsule whose pivot is its centre

        f.registerAgent(agent);
        const int index = f.agentOf(agent).crowdAgentIndex;
        REQUIRE(index >= 0);
        CHECK(f.agentOf(agent).isActive);
        CHECK(vecApprox(f.provider.agentPositions[index], kSite)); // spawned on the surface

        REQUIRE(f.staticWrites.size() == 1);
        CHECK(f.staticWrites[0].first == NavmeshFixture::handle(agent).id);
        CHECK_FALSE(f.staticWrites[0].second);

        // Write-back: the transform sits baseOffset above the point the crowd moved.
        f.provider.agentPositions[index] = kSite + glm::vec3(3.0f, 0.0f, 0.0f);
        f.service.update(0.016f, true);
        const auto& transform = scene::EntityRegistry::getRegistry().get<components::TransformComponent>(agent);
        CHECK(vecApprox(transform.position, kSite + glm::vec3(3.0f, 1.0f, 0.0f)));
    }

    TEST_CASE("stoppingDistance stops the crowd agent short of its destination")
    {
        NavmeshFixture f;
        f.provider.mesh = true; // destinations resolve onto the navmesh
        const glm::vec3 target = kSite + glm::vec3(10.0f, 0.0f, 0.0f);

        SUBCASE("shorter than arrivalDistance: arrive first, stop once within reach")
        {
            const entt::entity agent = f.makeAgent(kSite);
            f.agentOf(agent).stoppingDistance = 0.5f; // arrivalDistance stays 1.0

            f.setDestination(agent, target);
            const int index = f.agentOf(agent).crowdAgentIndex;
            REQUIRE(index >= 0);
            REQUIRE_FALSE(f.provider.targets.empty());
            CHECK(f.provider.targets.back().first == index);

            f.provider.agentPositions[index] = target - glm::vec3(2.0f, 0.0f, 0.0f);
            f.service.update(0.016f, true);
            CHECK(f.arrivals == 0);
            CHECK(f.provider.stoppedAgents.empty());

            f.provider.agentPositions[index] = target - glm::vec3(0.8f, 0.0f, 0.0f);
            f.service.update(0.016f, true);
            CHECK(f.arrivals == 1);
            CHECK(f.provider.stoppedAgents.empty()); // still closing in

            f.provider.agentPositions[index] = target - glm::vec3(0.4f, 0.0f, 0.0f);
            f.service.update(0.016f, true);
            CHECK(f.arrivals == 1);
            REQUIRE(f.provider.stoppedAgents.size() == 1);
            CHECK(f.provider.stoppedAgents[0] == index);
        }

        SUBCASE("longer than arrivalDistance: arrive and stop together")
        {
            const entt::entity agent = f.makeAgent(kSite);
            f.agentOf(agent).stoppingDistance = 1.5f;

            f.setDestination(agent, target);
            const int index = f.agentOf(agent).crowdAgentIndex;
            REQUIRE(index >= 0);

            f.provider.agentPositions[index] = target - glm::vec3(1.2f, 0.0f, 0.0f);
            f.service.update(0.016f, true);
            CHECK(f.arrivals == 1);
            REQUIRE(f.provider.stoppedAgents.size() == 1);
            CHECK(f.provider.stoppedAgents[0] == index);
        }

        SUBCASE("zero walks right to the point, as before")
        {
            const entt::entity agent = f.makeAgent(kSite);
            f.agentOf(agent).stoppingDistance = 0.0f;

            f.setDestination(agent, target);
            const int index = f.agentOf(agent).crowdAgentIndex;
            REQUIRE(index >= 0);

            f.provider.agentPositions[index] = target;
            f.service.update(0.016f, true);
            CHECK(f.arrivals == 1);
            CHECK(f.provider.stoppedAgents.empty());
        }
    }

    TEST_CASE("removing a NavmeshAgent component takes it out of the crowd first")
    {
        NavmeshFixture f;
        const entt::entity unit = f.make(kSite);
        auto& d = events::EventDispatcher::instance();

        events::scene::AddNavmeshAgentComponentCommand add;
        add.entity = NavmeshFixture::handle(unit);
        REQUIRE(d.execute(add));
        f.registerAgent(unit);
        const int index = f.agentOf(unit).crowdAgentIndex;
        REQUIRE(index >= 0);

        events::scene::RemoveNavmeshAgentComponentCommand remove;
        remove.entity = NavmeshFixture::handle(unit);
        REQUIRE(d.execute(remove));
        REQUIRE(f.provider.removedAgents.size() == 1);
        CHECK(f.provider.removedAgents[0] == index);
        CHECK_FALSE(scene::EntityRegistry::getRegistry().all_of<components::NavmeshAgentComponent>(unit));
    }

    TEST_CASE("a clear releases every crowd agent and resets its crowd state")
    {
        NavmeshFixture f;
        const entt::entity agent = f.makeAgent(kSite);
        f.registerAgent(agent);
        const int index = f.agentOf(agent).crowdAgentIndex;
        REQUIRE(index >= 0);

        events::EventDispatcher::instance().execute(events::navmesh::ClearNavmeshCommand{});

        REQUIRE(f.provider.removedAgents.size() == 1);
        CHECK(f.provider.removedAgents[0] == index);
        CHECK(f.provider.clearCalls == 1);
        CHECK_FALSE(f.agentOf(agent).isActive);
        CHECK(f.agentOf(agent).crowdAgentIndex == -1);
        CHECK_FALSE(f.agentData(agent)->inCrowd);
        CHECK_FALSE(f.status().unsavedChanges);
    }

    TEST_CASE("a bake releases the crowd agents and issues moves set meanwhile once it is harvested")
    {
        NavmeshFixture f;
        f.makeGround();
        const entt::entity agent = f.makeAgent(kSite);
        f.registerAgent(agent);
        const int before = f.agentOf(agent).crowdAgentIndex;
        REQUIRE(before >= 0);

        const auto ticket = f.requestBake();
        REQUIRE(ticket.accepted);
        REQUIRE(f.provider.removedAgents.size() == 1);
        CHECK(f.provider.removedAgents[0] == before);
        CHECK(f.agentOf(agent).crowdAgentIndex == -1);

        // The crowd is being rebuilt: the move waits instead of joining the old crowd.
        const glm::vec3 target = kSite + glm::vec3(4.0f, 0.0f, 4.0f);
        const size_t targetsBefore = f.provider.targets.size();
        const int agentsBefore = f.provider.nextAgent;
        f.setDestination(agent, target);
        CHECK(f.provider.targets.size() == targetsBefore);
        CHECK(f.provider.nextAgent == agentsBefore);

        REQUIRE(f.pumpUntilFinished(ticket.ticket).state == NavmeshBakeState::Succeeded);
        REQUIRE(f.provider.targets.size() == targetsBefore + 1);
        CHECK(vecApprox(f.provider.targets.back().second, target));
        CHECK(f.agentOf(agent).isActive);
        CHECK(f.agentOf(agent).crowdAgentIndex == f.provider.targets.back().first);
    }

    TEST_CASE("a bake sends the moves that were in progress again once it is harvested")
    {
        NavmeshFixture f;
        f.makeGround();
        f.provider.mesh = true; // an earlier navmesh: the destination resolves onto it
        const entt::entity agent = f.makeAgent(kSite);
        const glm::vec3 target = kSite + glm::vec3(6.0f, 0.0f, 2.0f);
        f.setDestination(agent, target); // joins the crowd and starts walking
        REQUIRE(f.agentOf(agent).crowdAgentIndex >= 0);
        const size_t targetsBefore = f.provider.targets.size();

        const auto ticket = f.requestBake();
        REQUIRE(ticket.accepted);
        CHECK(f.agentOf(agent).crowdAgentIndex == -1);

        REQUIRE(f.pumpUntilFinished(ticket.ticket).state == NavmeshBakeState::Succeeded);
        REQUIRE(f.provider.targets.size() == targetsBefore + 1);
        CHECK(vecApprox(f.provider.targets.back().second, target));
        CHECK(f.agentOf(agent).isActive);
    }

    TEST_CASE("NavRoute components are added and removed through the scene events")
    {
        NavmeshFixture f;
        const entt::entity route = f.make(kSite);
        auto& d = events::EventDispatcher::instance();

        events::scene::AddNavRouteComponentCommand add;
        add.entity = NavmeshFixture::handle(route);
        CHECK(d.execute(add));
        CHECK(scene::EntityRegistry::getRegistry().all_of<components::NavRouteComponent>(route));
        CHECK_FALSE(d.execute(add)); // already a route

        events::scene::RemoveNavRouteComponentCommand remove;
        remove.entity = NavmeshFixture::handle(route);
        CHECK(d.execute(remove));
        CHECK_FALSE(scene::EntityRegistry::getRegistry().all_of<components::NavRouteComponent>(route));
        CHECK_FALSE(d.execute(remove));

        add.entity = services::EntityHandle::invalid();
        CHECK_FALSE(d.execute(add));
    }

    TEST_CASE("a new scene or a disk load clears the navmesh; a Play->Stop snapshot restore keeps it")
    {
        NavmeshFixture f;
        stubSceneLoadCommands();
        auto sceneGraph = std::make_shared<scene::SceneGraphSystem>();
        services::ScenePersistenceService persistence(sceneGraph, nullptr);
        f.provider.mesh = true;

        REQUIRE(persistence.captureSnapshot());
        REQUIRE(persistence.restoreSnapshot());
        persistence.update();
        CHECK(f.provider.clearCalls == 0);
        CHECK(f.provider.hasNavmesh());

        const fs::path directory = fs::temp_directory_path() / "vf_navmesh_scene_change_tests";
        std::error_code ec;
        fs::remove_all(directory, ec);
        fs::create_directories(directory);
        const fs::path scenePath = directory / "NavmeshClear.vfScene";
        REQUIRE(persistence.saveScene(scenePath.string()));
        REQUIRE(persistence.loadScene(scenePath.string()));
        persistence.update();
        CHECK(f.provider.clearCalls == 1);
        CHECK_FALSE(f.provider.hasNavmesh());

        f.provider.mesh = true;
        REQUIRE(persistence.newScene());
        CHECK(f.provider.clearCalls == 2);
        CHECK_FALSE(f.provider.hasNavmesh());

        sceneGraph->clearScene();
        auto& registry = scene::EntityRegistry::getRegistry();
        const entt::entity sceneRoot = sceneGraph->GetRoot().getHandle();
        if (registry.valid(sceneRoot))
            registry.destroy(sceneRoot);
        fs::remove_all(directory, ec);
    }
}
