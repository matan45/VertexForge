#include <doctest.h>
#include "impl/volumetric/VolumetricAgentManager.hpp"
#include "providers/volumetric/IVolumetricNavProvider.hpp"
#include "events/EventDispatcher.hpp"
#include "events/scene/EntityTransformEvents.hpp"
#include "events/volumetric/VolumetricNavEvents.hpp"
#include "data/EntityConversion.hpp"
#include "components/Components.hpp"
#include "scene/Entity.hpp"
#include "scene/EntityRegistry.hpp"
#include "scene/HierarchyMatrix.hpp"

#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// ============================================================
// VK-1660: VolumetricAgentManager - lazy registration, arrive steering, failures, stuck detection, the
// world -> local write-back under a parent, replanning after a grid swap, and clearing.
// ============================================================

namespace
{
    namespace vol = volumetric;

    constexpr uint64_t kVolume = 7;

    struct DispatcherScope
    {
        DispatcherScope() { events::EventDispatcher::instance().clear(); }
        ~DispatcherScope() { events::EventDispatcher::instance().clear(); }
    };

    // Answers every path request with `nextPath`; volume kVolume's grid is `grid` (only its identity matters).
    struct FakeProvider : services::IVolumetricNavProvider
    {
        std::shared_ptr<const vol::VoxelGrid> grid = std::make_shared<const vol::VoxelGrid>();
        vol::VolumePathResult nextPath;
        mutable int findPathCalls = 0;
        mutable vol::PathRequest lastRequest;

        void setVolume(uint64_t, std::shared_ptr<const vol::VoxelGrid> value) override { grid = std::move(value); }
        void removeVolume(uint64_t) override { grid.reset(); }
        void clearVolumes() override { grid.reset(); }
        std::shared_ptr<const vol::VoxelGrid> getVolume(uint64_t id) const override
        {
            return id == kVolume ? grid : nullptr;
        }
        std::vector<uint64_t> getVolumeIds() const override
        {
            return grid ? std::vector<uint64_t>{kVolume} : std::vector<uint64_t>{};
        }
        uint64_t volumeAt(const glm::vec3&) const override { return grid ? kVolume : 0; }
        vol::VolumePathResult findPath3D(const vol::PathRequest& request) const override
        {
            ++findPathCalls;
            lastRequest = request;
            return nextPath;
        }
        vol::VolumetricNavWorld::PointQuery queryPoint(const glm::vec3&, float) const override { return {}; }
    };

    vol::VolumePathResult straightPath(const glm::vec3& from, const glm::vec3& to,
                                       vol::VolumePathStatus status = vol::VolumePathStatus::Found)
    {
        vol::VolumePathResult result;
        result.status = status;
        result.waypoints = {from, to};
        result.volumeId = kVolume;
        return result;
    }

    // An entity in the global registry, destroyed at scope exit.
    class ScopedEntity
    {
    public:
        ScopedEntity(const std::string& name, const glm::vec3& position)
        {
            scene::EntityRegistry::init();
            scene::Entity entity(name);
            entity.getComponent<components::TransformComponent>().position = position;
            handle = entity.getHandle();
        }

        ~ScopedEntity()
        {
            auto& registry = scene::EntityRegistry::getRegistry();
            if (handle != entt::null && registry.valid(handle))
                registry.destroy(handle);
        }

        ScopedEntity(const ScopedEntity&) = delete;
        ScopedEntity& operator=(const ScopedEntity&) = delete;

        entt::entity get() const { return handle; }
        services::EntityHandle entityHandle() const { return services::internal::toHandle(handle); }
        components::TransformComponent& transform() const
        {
            return scene::EntityRegistry::getRegistry().get<components::TransformComponent>(handle);
        }
        glm::vec3 worldPosition() const
        {
            return scene::composeWorldPosition(scene::EntityRegistry::getRegistry(), handle);
        }

    private:
        entt::entity handle{entt::null};
    };

    components::VolumetricAgentComponent& addAgent(const ScopedEntity& entity, float maxSpeed = 5.0f,
                                                    float maxAcceleration = 20.0f, float stoppingDistance = 0.5f)
    {
        auto& agent = scene::EntityRegistry::getRegistry().emplace_or_replace<components::VolumetricAgentComponent>(
            entity.get());
        agent.maxSpeed = maxSpeed;
        agent.maxAcceleration = maxAcceleration;
        agent.stoppingDistance = stoppingDistance;
        return agent;
    }

    bool isActive(const ScopedEntity& entity)
    {
        return scene::EntityRegistry::getRegistry().get<components::VolumetricAgentComponent>(entity.get()).isActive;
    }

    // Steps until the agent is no longer Moving (or maxSteps), returning its state.
    services::VolumetricAgentMoveState settle(services::VolumetricAgentManager& manager, services::EntityHandle entity,
                                              float deltaTime = 0.05f, int maxSteps = 400)
    {
        for (int i = 0; i < maxSteps; ++i)
        {
            manager.update(deltaTime);
            const auto state = manager.getState(entity);
            if (!state || state->state != services::VolumetricAgentMoveState::Moving)
                break;
        }
        const auto state = manager.getState(entity);
        return state ? state->state : services::VolumetricAgentMoveState::Idle;
    }

    struct EventCounter
    {
        int reached = 0;
        int failed = 0;
        std::string lastReason;
        events::ScopedSubscription reachedSubscription;
        events::ScopedSubscription failedSubscription;

        EventCounter()
        {
            auto& dispatcher = events::EventDispatcher::instance();
            reachedSubscription = events::ScopedSubscription(
                dispatcher.subscribe<events::volumetric::VolumetricAgentReachedDestinationNotification>(
                    [this](const events::volumetric::VolumetricAgentReachedDestinationNotification&) { ++reached; }));
            failedSubscription = events::ScopedSubscription(
                dispatcher.subscribe<events::volumetric::VolumetricAgentPathFailedNotification>(
                    [this](const events::volumetric::VolumetricAgentPathFailedNotification& notification)
                    {
                        ++failed;
                        lastReason = notification.reason;
                    }));
        }
    };

    // Counts the SetEntityStaticCommands the manager sends (it marks a static agent dynamic once, on registration).
    struct StaticCommandCounter
    {
        int count = 0;
        bool lastIsStatic = true;

        StaticCommandCounter()
        {
            events::EventDispatcher::instance().registerCommandHandler<events::scene::SetEntityStaticCommand>(
                [this](const events::scene::SetEntityStaticCommand& command)
                {
                    ++count;
                    lastIsStatic = command.isStatic;
                    return true;
                });
        }

        ~StaticCommandCounter()
        {
            events::EventDispatcher::instance().unregisterCommandHandler<events::scene::SetEntityStaticCommand>();
        }
    };
}

TEST_SUITE("VolumetricAgentManager")
{
    TEST_CASE("a destination registers the agent, which flies there and arrives within stoppingDistance")
    {
        DispatcherScope dispatcher;
        FakeProvider provider;
        services::VolumetricAgentManager manager(&provider);
        EventCounter counter;
        StaticCommandCounter staticCommands;

        ScopedEntity flyer("Flyer", glm::vec3(0.0f));
        addAgent(flyer);
        provider.nextPath = straightPath(glm::vec3(0.0f), glm::vec3(10.0f, 0.0f, 0.0f));

        CHECK_FALSE(manager.isRegistered(flyer.entityHandle()));
        REQUIRE(manager.setDestination(flyer.entityHandle(), glm::vec3(10.0f, 0.0f, 0.0f)));
        CHECK(manager.isRegistered(flyer.entityHandle()));
        CHECK(isActive(flyer));
        CHECK(staticCommands.count == 1);
        CHECK_FALSE(staticCommands.lastIsStatic);
        CHECK(provider.findPathCalls == 1);
        CHECK(provider.lastRequest.end == glm::vec3(10.0f, 0.0f, 0.0f));
        REQUIRE(manager.getState(flyer.entityHandle()).has_value());
        CHECK(manager.getState(flyer.entityHandle())->state == services::VolumetricAgentMoveState::Moving);
        CHECK(manager.getState(flyer.entityHandle())->remaining == doctest::Approx(10.0f));

        const auto paths = manager.activePaths();
        REQUIRE(paths.size() == 1);
        CHECK(paths[0].front() == glm::vec3(0.0f));
        CHECK(paths[0].back() == glm::vec3(10.0f, 0.0f, 0.0f));

        CHECK(settle(manager, flyer.entityHandle()) == services::VolumetricAgentMoveState::Arrived);
        CHECK(counter.reached == 1);
        CHECK(counter.failed == 0);
        const glm::vec3 position = flyer.worldPosition();
        CHECK(glm::distance(position, glm::vec3(10.0f, 0.0f, 0.0f)) <= 0.5f + 1.0e-3f);
        CHECK(flyer.transform().rotation.y == doctest::Approx(90.0f).epsilon(0.01)); // faces +X
        CHECK(manager.activePaths().empty());
    }

    TEST_CASE("a path the volumes cannot give fails the move at once")
    {
        DispatcherScope dispatcher;
        FakeProvider provider;
        services::VolumetricAgentManager manager(&provider);
        EventCounter counter;

        ScopedEntity flyer("Flyer", glm::vec3(0.0f));
        addAgent(flyer);
        provider.nextPath = vol::VolumePathResult{};
        provider.nextPath.status = vol::VolumePathStatus::NoVolume;

        REQUIRE(manager.setDestination(flyer.entityHandle(), glm::vec3(5.0f)));
        const auto state = manager.getState(flyer.entityHandle());
        REQUIRE(state.has_value());
        CHECK(state->state == services::VolumetricAgentMoveState::Failed);
        CHECK(state->failReason == std::string(vol::volumePathStatusName(vol::VolumePathStatus::NoVolume)));
        CHECK(counter.failed == 1);
        CHECK(counter.lastReason == state->failReason);
    }

    TEST_CASE("an agent that makes no progress fails as Stuck")
    {
        DispatcherScope dispatcher;
        FakeProvider provider;
        services::VolumetricAgentManager manager(&provider);

        ScopedEntity flyer("Flyer", glm::vec3(0.0f));
        addAgent(flyer, 0.0f); // maxSpeed 0: it never moves
        provider.nextPath = straightPath(glm::vec3(0.0f), glm::vec3(10.0f, 0.0f, 0.0f));
        REQUIRE(manager.setDestination(flyer.entityHandle(), glm::vec3(10.0f, 0.0f, 0.0f)));

        for (int i = 0; i < 10; ++i)
            manager.update(0.1f); // 1 s: not yet
        CHECK(manager.getState(flyer.entityHandle())->state == services::VolumetricAgentMoveState::Moving);
        for (int i = 0; i < 10; ++i)
            manager.update(0.1f); // 2 s
        const auto state = manager.getState(flyer.entityHandle());
        CHECK(state->state == services::VolumetricAgentMoveState::Failed);
        CHECK(state->failReason == "Stuck");
    }

    TEST_CASE("the agent moves in world space and writes a local position under its parent")
    {
        DispatcherScope dispatcher;
        FakeProvider provider;
        services::VolumetricAgentManager manager(&provider);

        ScopedEntity carrier("Carrier", glm::vec3(100.0f, 0.0f, 0.0f));
        ScopedEntity flyer("Flyer", glm::vec3(0.0f));
        scene::EntityRegistry::getRegistry().emplace_or_replace<components::ParentComponent>(
            flyer.get(), components::ParentComponent{carrier.get()});
        addAgent(flyer);
        REQUIRE(flyer.worldPosition().x == doctest::Approx(100.0f));

        provider.nextPath = straightPath(glm::vec3(100.0f, 0.0f, 0.0f), glm::vec3(110.0f, 0.0f, 0.0f));
        REQUIRE(manager.setDestination(flyer.entityHandle(), glm::vec3(110.0f, 0.0f, 0.0f)));
        CHECK(provider.lastRequest.start == glm::vec3(100.0f, 0.0f, 0.0f)); // planned from the WORLD position
        CHECK(settle(manager, flyer.entityHandle()) == services::VolumetricAgentMoveState::Arrived);

        const glm::vec3 world = flyer.worldPosition();
        CHECK(glm::distance(world, glm::vec3(110.0f, 0.0f, 0.0f)) <= 0.5f + 1.0e-3f);
        CHECK(flyer.transform().position.x == doctest::Approx(world.x - 100.0f));
    }

    TEST_CASE("a replaced volume grid replans the path; a partial path is replanned at its end")
    {
        DispatcherScope dispatcher;
        FakeProvider provider;
        services::VolumetricAgentManager manager(&provider);

        ScopedEntity flyer("Flyer", glm::vec3(0.0f));
        addAgent(flyer);
        provider.nextPath = straightPath(glm::vec3(0.0f), glm::vec3(5.0f, 0.0f, 0.0f), vol::VolumePathStatus::Partial);
        REQUIRE(manager.setDestination(flyer.entityHandle(), glm::vec3(10.0f, 0.0f, 0.0f)));
        CHECK(manager.getState(flyer.entityHandle())->partial);
        CHECK(provider.findPathCalls == 1);

        SUBCASE("grid swap")
        {
            provider.grid = std::make_shared<const vol::VoxelGrid>(); // a re-bake replaced the volume
            manager.update(0.05f);
            CHECK(provider.findPathCalls == 2);
            CHECK(manager.getState(flyer.entityHandle())->state == services::VolumetricAgentMoveState::Moving);
        }

        SUBCASE("partial end")
        {
            provider.nextPath = straightPath(glm::vec3(5.0f, 0.0f, 0.0f), glm::vec3(10.0f, 0.0f, 0.0f));
            CHECK(settle(manager, flyer.entityHandle()) == services::VolumetricAgentMoveState::Arrived);
            CHECK(provider.findPathCalls == 2);
            CHECK(glm::distance(flyer.worldPosition(), glm::vec3(10.0f, 0.0f, 0.0f)) <= 0.5f + 1.0e-3f);
        }

        SUBCASE("a partial path that gets no nearer fails")
        {
            // The replan at the partial end returns the same end again: no further progress is possible.
            CHECK(settle(manager, flyer.entityHandle()) == services::VolumetricAgentMoveState::Failed);
            CHECK(manager.getState(flyer.entityHandle())->failReason ==
                  std::string(vol::volumePathStatusName(vol::VolumePathStatus::NoPath)));
        }
    }

    TEST_CASE("a new destination is Moving at once, stop is Idle, clear and removal forget the agent")
    {
        DispatcherScope dispatcher;
        FakeProvider provider;
        services::VolumetricAgentManager manager(&provider);
        StaticCommandCounter staticCommands;

        ScopedEntity flyer("Flyer", glm::vec3(0.0f));
        addAgent(flyer);
        provider.nextPath = straightPath(glm::vec3(0.0f), glm::vec3(2.0f, 0.0f, 0.0f));
        REQUIRE(manager.setDestination(flyer.entityHandle(), glm::vec3(2.0f, 0.0f, 0.0f)));
        REQUIRE(settle(manager, flyer.entityHandle()) == services::VolumetricAgentMoveState::Arrived);

        // The previous move's Arrived is never seen after a new destination.
        provider.nextPath = straightPath(flyer.worldPosition(), glm::vec3(6.0f, 0.0f, 0.0f));
        REQUIRE(manager.setDestination(flyer.entityHandle(), glm::vec3(6.0f, 0.0f, 0.0f)));
        CHECK(manager.getState(flyer.entityHandle())->state == services::VolumetricAgentMoveState::Moving);
        CHECK(manager.getState(flyer.entityHandle())->destination == glm::vec3(6.0f, 0.0f, 0.0f));
        CHECK(staticCommands.count == 1); // marked dynamic on registration only

        manager.stopAgent(flyer.entityHandle());
        CHECK(manager.getState(flyer.entityHandle())->state == services::VolumetricAgentMoveState::Idle);
        CHECK(manager.isRegistered(flyer.entityHandle()));

        SUBCASE("clear")
        {
            manager.clear();
            CHECK_FALSE(manager.isRegistered(flyer.entityHandle()));
            CHECK_FALSE(isActive(flyer));
        }

        SUBCASE("remove")
        {
            manager.removeAgent(flyer.entityHandle());
            CHECK_FALSE(manager.isRegistered(flyer.entityHandle()));
            CHECK_FALSE(isActive(flyer));
            CHECK(manager.getState(flyer.entityHandle())->state == services::VolumetricAgentMoveState::Idle);
        }

        SUBCASE("the component is removed")
        {
            scene::EntityRegistry::getRegistry().remove<components::VolumetricAgentComponent>(flyer.get());
            CHECK_FALSE(manager.getState(flyer.entityHandle()).has_value());
            manager.update(0.05f);
            CHECK_FALSE(manager.isRegistered(flyer.entityHandle()));
        }
    }

    TEST_CASE("an entity without a VolumetricAgent is refused")
    {
        DispatcherScope dispatcher;
        FakeProvider provider;
        services::VolumetricAgentManager manager(&provider);
        ScopedEntity plain("Plain", glm::vec3(0.0f));
        CHECK_FALSE(manager.setDestination(plain.entityHandle(), glm::vec3(1.0f)));
        CHECK_FALSE(manager.isRegistered(plain.entityHandle()));
        CHECK_FALSE(manager.getState(plain.entityHandle()).has_value());
    }
}
