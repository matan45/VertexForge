#include <doctest.h>
#include "impl/volumetric/VolumetricNavServiceImpl.hpp"
#include "impl/navigation/SceneGeometryCollector.hpp"
#include "providers/volumetric/IVolumetricNavProvider.hpp"
#include "events/EventDispatcher.hpp"
#include "events/editor/EditorModeEvents.hpp"
#include "events/project/ResourceEvents.hpp"
#include "events/render/DebugDrawEvents.hpp"
#include "events/volumetric/VolumetricNavEvents.hpp"
#include "data/EntityConversion.hpp"
#include "components/Components.hpp"
#include "navigation/volumetric/VolumetricNavWorld.hpp"
#include "navigation/volumetric/VolumetricSerializer.hpp"
#include "resource/VirtualFileSystem.hpp"
#include "scene/Entity.hpp"
#include "scene/EntityRegistry.hpp"
#include "scene/HierarchyMatrix.hpp"

#include <glm/glm.hpp>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

// ============================================================
// VK-1660: VolumetricNavServiceImpl - the bake lifecycle (main-thread snapshot -> JobSystem -> harvest in
// update()), refusals, cancel / clear / orphaned bakes, .vfVolNav save -> unload -> reload by UUID -> clear,
// stale grids, the component data validation and the agent path through the events.
// ============================================================

namespace
{
    namespace fs = std::filesystem;

    // The engine's provider (core::VolumetricNavAdapter) forwards to a VolumetricNavWorld; Tests does not link
    // Core, so this is the same forwarder.
    struct WorldProvider : services::IVolumetricNavProvider
    {
        volumetric::VolumetricNavWorld world;

        void setVolume(uint64_t volumeId, std::shared_ptr<const volumetric::VoxelGrid> grid) override
        {
            world.setVolume(volumeId, std::move(grid));
        }
        void removeVolume(uint64_t volumeId) override { world.removeVolume(volumeId); }
        void clearVolumes() override { world.clear(); }
        std::shared_ptr<const volumetric::VoxelGrid> getVolume(uint64_t volumeId) const override
        {
            return world.getVolume(volumeId);
        }
        std::vector<uint64_t> getVolumeIds() const override { return world.volumeIds(); }
        uint64_t volumeAt(const glm::vec3& point) const override { return world.volumeAt(point); }
        volumetric::VolumePathResult findPath3D(const volumetric::PathRequest& request) const override
        {
            return world.findPath(request);
        }
        volumetric::VolumetricNavWorld::PointQuery queryPoint(const glm::vec3& point, float snapRadius) const override
        {
            return world.queryPoint(point, snapRadius);
        }
    };

    // A service with its handlers on a cleared dispatcher.
    struct ServiceFixture
    {
        WorldProvider provider;
        std::unique_ptr<services::VolumetricNavServiceImpl> service;

        ServiceFixture()
        {
            scene::EntityRegistry::init();
            events::EventDispatcher::instance().clear();
            service = std::make_unique<services::VolumetricNavServiceImpl>(&provider);
            service->registerEventHandlers();
        }

        ~ServiceFixture()
        {
            service.reset();
            events::EventDispatcher::instance().clear();
        }

        ServiceFixture(const ServiceFixture&) = delete;
        ServiceFixture& operator=(const ServiceFixture&) = delete;

        // Harvests until the bake is no longer Running (an uninitialized JobSystem runs it inline; an initialized
        // one on a worker).
        services::VolumetricBakeOutcome finish(uint64_t ticket)
        {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
            services::VolumetricBakeOutcome outcome = service->getBakeOutcome(ticket);
            while (outcome.state == services::VolumetricBakeState::Running && std::chrono::steady_clock::now() < deadline)
            {
                service->update(0.016f, false);
                outcome = service->getBakeOutcome(ticket);
                if (outcome.state == services::VolumetricBakeState::Running)
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return outcome;
        }

        services::VolumetricBakeState bake(services::EntityHandle volume)
        {
            const services::VolumetricBakeTicket ticket = service->requestBake(volume);
            return ticket.accepted ? finish(ticket.ticket).state : services::VolumetricBakeState::Unknown;
        }

        static void setPlayMode(bool play)
        {
            events::editor::EditorModeChangedNotification notification;
            notification.previousMode = play ? services::EditorMode::Edit : services::EditorMode::Play;
            notification.currentMode = play ? services::EditorMode::Play : services::EditorMode::Edit;
            events::EventDispatcher::instance().publish(notification);
        }
    };

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

        ~ScopedEntity() { destroy(); }

        ScopedEntity(const ScopedEntity&) = delete;
        ScopedEntity& operator=(const ScopedEntity&) = delete;

        void destroy()
        {
            auto& registry = scene::EntityRegistry::getRegistry();
            if (handle != entt::null && registry.valid(handle))
                registry.destroy(handle);
            handle = entt::null;
        }

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

    // A volume with a fixed UUID and no geometry sources: terrain / mesh / foliage queries have no handlers here,
    // and colliders other suites leave in the global registry must not leak into the bake.
    void addVolume(const ScopedEntity& entity, uint64_t uuid, const glm::vec3& halfExtents)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        registry.emplace_or_replace<components::UUIDComponent>(entity.get(), uuid);
        auto& volume = registry.emplace_or_replace<components::VolumetricNavVolumeComponent>(entity.get());
        volume.halfExtents = halfExtents;
        volume.voxelSize = 1.0f;
        volume.includeTerrain = false;
        volume.includeStaticMeshes = false;
        volume.includeColliders = false;
    }

    // Fetched on each use: a reference into the component storage does not survive later removals.
    components::VolumetricNavVolumeComponent& volumeOf(const ScopedEntity& entity)
    {
        return scene::EntityRegistry::getRegistry().get<components::VolumetricNavVolumeComponent>(entity.get());
    }
}

TEST_SUITE("VolumetricNavService")
{
    TEST_CASE("a bake runs on the JobSystem, is harvested by update() and answers the queries")
    {
        ServiceFixture fixture;
        auto& dispatcher = events::EventDispatcher::instance();
        constexpr uint64_t kUuid = 916801;
        ScopedEntity sky("Sky", glm::vec3(0.0f));
        addVolume(sky, kUuid, glm::vec3(2.0f));

        int completions = 0;
        events::volumetric::VolumetricBakeCompleteNotification completion;
        events::ScopedSubscription subscription(
            dispatcher.subscribe<events::volumetric::VolumetricBakeCompleteNotification>(
                [&](const events::volumetric::VolumetricBakeCompleteNotification& notification)
                {
                    ++completions;
                    completion = notification;
                }));

        events::volumetric::RequestVolumetricBakeCommand request;
        request.volume = sky.entityHandle();
        const services::VolumetricBakeTicket ticket = dispatcher.execute(request);
        REQUIRE(ticket.accepted);
        CHECK(ticket.ticket != 0);
        CHECK(ticket.error.empty());

        // Running until update() harvests it, even when the job already ran inline.
        CHECK(fixture.service->getBakeOutcome(ticket.ticket).state == services::VolumetricBakeState::Running);
        CHECK(fixture.service->getVolumeStatus(sky.entityHandle())->bakeState == services::VolumetricBakeState::Running);
        CHECK(completions == 0);

        const services::VolumetricBakeOutcome outcome = fixture.finish(ticket.ticket);
        REQUIRE(outcome.state == services::VolumetricBakeState::Succeeded);
        CHECK(outcome.volume == sky.entityHandle());
        CHECK(outcome.dims == glm::ivec3(4));
        CHECK(outcome.voxels == 64);
        CHECK(outcome.navigable == 64); // no geometry: everything is open space
        CHECK(outcome.progress == doctest::Approx(1.0f));
        CHECK(completions == 1);
        CHECK(completion.success);
        CHECK(completion.ticket == ticket.ticket);
        CHECK(completion.volume == sky.entityHandle());

        events::volumetric::GetVolumetricBakeOutcomeQuery outcomeQuery;
        outcomeQuery.ticket = ticket.ticket;
        CHECK(dispatcher.query(outcomeQuery).state == services::VolumetricBakeState::Succeeded);

        const std::optional<services::VolumetricVolumeStatus> status = fixture.service->getVolumeStatus(sky.entityHandle());
        REQUIRE(status.has_value());
        CHECK(status->baked);
        CHECK_FALSE(status->stale);
        CHECK(status->uuid == kUuid);
        CHECK(status->dims == glm::ivec3(4));
        CHECK(status->navigablePercent == doctest::Approx(100.0f)); // 0-100, not a fraction
        CHECK(status->bakeState == services::VolumetricBakeState::Succeeded);
        CHECK(status->file.empty()); // not saved yet
        CHECK(volumeOf(sky).isBaked);
        CHECK(fixture.provider.getVolume(kUuid).get() != nullptr);

        const std::vector<services::VolumetricVolumeStatus> statuses = fixture.service->listVolumes();
        CHECK(std::any_of(statuses.begin(), statuses.end(),
                          [&](const services::VolumetricVolumeStatus& entry) { return entry.uuid == kUuid; }));

        const std::optional<services::VolumetricNavVolumeData> data = fixture.service->getVolumeData(sky.entityHandle());
        REQUIRE(data.has_value());
        CHECK(data->baked);
        CHECK(data->voxels == 64);
        CHECK(data->navigablePercent == doctest::Approx(100.0f));

        // Outside every loaded volume there is no nearest point.
        events::volumetric::GetNearestNavigable3DQuery nearest;
        nearest.point = glm::vec3(100.0f, 0.0f, 0.0f);
        CHECK_FALSE(dispatcher.query(nearest).has_value());
        nearest.point = glm::vec3(0.5f);
        const std::optional<glm::vec3> inside = dispatcher.query(nearest);
        REQUIRE(inside.has_value());
        CHECK(glm::distance(*inside, glm::vec3(0.5f)) < 1.0e-4f);

        events::volumetric::IsPointNavigable3DQuery navigable;
        navigable.point = glm::vec3(0.5f);
        CHECK(dispatcher.query(navigable));
        navigable.point = glm::vec3(100.0f);
        CHECK_FALSE(dispatcher.query(navigable));

        events::volumetric::FindPath3DQuery path;
        path.start = glm::vec3(-1.5f);
        path.end = glm::vec3(1.5f);
        const volumetric::VolumePathResult found = dispatcher.query(path);
        CHECK(found.status == volumetric::VolumePathStatus::Found);
        CHECK(found.volumeId == kUuid);

        // The grid of a deleted volume is dropped on the next update().
        sky.destroy();
        fixture.service->update(0.016f, false);
        CHECK(fixture.provider.getVolume(kUuid).get() == nullptr);
    }

    TEST_CASE("a bake is refused without a volume, while one runs, for invalid values, over the cap and in Play")
    {
        ServiceFixture fixture;

        ScopedEntity plain("Plain", glm::vec3(0.0f));
        const services::VolumetricBakeTicket noVolume = fixture.service->requestBake(plain.entityHandle());
        CHECK_FALSE(noVolume.accepted);
        CHECK(noVolume.ticket == 0);
        CHECK_FALSE(noVolume.error.empty());

        ScopedEntity sky("Sky", glm::vec3(0.0f));
        addVolume(sky, 916802, glm::vec3(2.0f));
        const services::VolumetricBakeTicket first = fixture.service->requestBake(sky.entityHandle());
        REQUIRE(first.accepted);
        const services::VolumetricBakeTicket second = fixture.service->requestBake(sky.entityHandle());
        CHECK_FALSE(second.accepted);
        CHECK(second.error.find("already running") != std::string::npos);
        CHECK(fixture.finish(first.ticket).state == services::VolumetricBakeState::Succeeded);

        ScopedEntity huge("Huge", glm::vec3(0.0f));
        addVolume(huge, 916803, glm::vec3(200.0f));
        volumeOf(huge).voxelSize = 0.1f; // 4000^3 voxels
        const services::VolumetricBakeTicket capped = fixture.service->requestBake(huge.entityHandle());
        CHECK_FALSE(capped.accepted);
        CHECK(capped.error.find("raise voxelSize") != std::string::npos);

        volumeOf(huge).voxelSize = 4.0f;
        volumeOf(huge).connectivity = 7;
        const services::VolumetricBakeTicket invalid = fixture.service->requestBake(huge.entityHandle());
        CHECK_FALSE(invalid.accepted);
        CHECK(invalid.error.find("connectivity") != std::string::npos);

        ServiceFixture::setPlayMode(true);
        CHECK(fixture.service->isPlayMode());
        const services::VolumetricBakeTicket inPlay = fixture.service->requestBake(sky.entityHandle());
        CHECK_FALSE(inPlay.accepted);
        CHECK(inPlay.error.find("Play") != std::string::npos);
        ServiceFixture::setPlayMode(false);
        CHECK_FALSE(fixture.service->isPlayMode());
    }

    TEST_CASE("a cancelled, cleared or orphaned bake loads no grid")
    {
        ServiceFixture fixture;
        constexpr uint64_t kUuid = 916804;
        ScopedEntity sky("Sky", glm::vec3(0.0f));
        addVolume(sky, kUuid, glm::vec3(2.0f));
        const services::VolumetricBakeTicket ticket = fixture.service->requestBake(sky.entityHandle());
        REQUIRE(ticket.accepted);

        SUBCASE("cancel")
        {
            events::volumetric::CancelVolumetricBakeCommand cancel;
            cancel.volume = sky.entityHandle();
            events::EventDispatcher::instance().execute(cancel);
            CHECK(fixture.finish(ticket.ticket).state == services::VolumetricBakeState::Cancelled);
            const std::optional<services::VolumetricVolumeStatus> status = fixture.service->getVolumeStatus(sky.entityHandle());
            REQUIRE(status.has_value());
            CHECK(status->bakeState == services::VolumetricBakeState::Cancelled);
            CHECK_FALSE(status->baked);
            CHECK_FALSE(volumeOf(sky).isBaked);
        }

        SUBCASE("clear")
        {
            fixture.service->clearVolume(sky.entityHandle());
            CHECK(fixture.service->getVolumeStatus(sky.entityHandle())->bakeState != services::VolumetricBakeState::Running);
            CHECK(fixture.finish(ticket.ticket).state == services::VolumetricBakeState::Cancelled);
            CHECK_FALSE(fixture.service->getVolumeStatus(sky.entityHandle())->baked);
        }

        SUBCASE("the volume entity is deleted")
        {
            sky.destroy();
            const services::VolumetricBakeOutcome outcome = fixture.finish(ticket.ticket);
            CHECK(outcome.state == services::VolumetricBakeState::Failed);
            CHECK(outcome.message.find("no longer exists") != std::string::npos);
        }

        CHECK(fixture.provider.getVolume(kUuid).get() == nullptr);
    }

    TEST_CASE("a saved volume reloads by UUID after an unload; clear forgets the file but keeps it on disk")
    {
        ServiceFixture fixture;
        auto& dispatcher = events::EventDispatcher::instance();
        constexpr uint64_t kUuid = 916805; // 0xdfd45
        const fs::path directory = fs::temp_directory_path() / "vf_volumetric_service_tests";
        std::error_code ec;
        fs::remove_all(directory, ec);

        {
            ScopedEntity sky("Sky Volume", glm::vec3(0.0f));
            addVolume(sky, kUuid, glm::vec3(2.0f, 1.0f, 2.0f));

            // Nothing to save before a bake.
            CHECK_FALSE(fixture.service->saveVolume(sky.entityHandle(), directory.generic_string()).success);
            REQUIRE(fixture.bake(sky.entityHandle()) == services::VolumetricBakeState::Succeeded);

            std::string savedPath;
            events::ScopedSubscription savedSubscription(dispatcher.subscribe<events::resource::AssetSavedNotification>(
                [&savedPath](const events::resource::AssetSavedNotification& notification)
                {
                    savedPath = notification.filePath;
                }));

            events::volumetric::SaveVolumetricVolumeCommand save;
            save.volume = sky.entityHandle();
            save.directory = directory.generic_string();
            const services::VolumetricSaveResult saved = dispatcher.execute(save);
            REQUIRE_MESSAGE(saved.success, saved.error);
            CHECK(fs::exists(saved.path));
            CHECK(fs::path(saved.path).filename().string() == "Sky_Volume_00000000000dfd45.vfVolNav");
            CHECK(savedPath == saved.path);
            CHECK(volumeOf(sky).bakedRef.isValid());
            CHECK(volumeOf(sky).isBaked);
            CHECK(fixture.service->getVolumeStatus(sky.entityHandle())->file == saved.path);

            std::string error;
            const std::vector<uint8_t> bytes = resource::VirtualFileSystem::instance().readFile(saved.path);
            const std::optional<volumetric::VoxelGrid> decoded = volumetric::decodeVolume(bytes, error);
            REQUIRE_MESSAGE(decoded.has_value(), error);
            CHECK(decoded->dims() == glm::ivec3(4, 2, 4));

            // Loading again keeps the grid already loaded from the same file.
            const std::shared_ptr<const volumetric::VoxelGrid> before = fixture.provider.getVolume(kUuid);
            REQUIRE(before.get() != nullptr);
            CHECK(fixture.service->loadSceneVolumes() >= 1);
            CHECK(fixture.provider.getVolume(kUuid).get() == before.get());

            // A scene change unloads every grid but keeps the reference; the scene's load reads the file again.
            dispatcher.execute(events::volumetric::UnloadVolumetricNavCommand{});
            CHECK(fixture.provider.getVolume(kUuid).get() == nullptr);
            CHECK_FALSE(volumeOf(sky).isBaked);
            CHECK(volumeOf(sky).bakedRef.isValid());

            const int loaded = dispatcher.execute(events::volumetric::LoadSceneVolumetricVolumesCommand{});
            CHECK(loaded >= 1);
            const std::shared_ptr<const volumetric::VoxelGrid> reloaded = fixture.provider.getVolume(kUuid);
            REQUIRE(reloaded.get() != nullptr);
            CHECK(reloaded.get() != before.get());
            CHECK(reloaded->dims() == glm::ivec3(4, 2, 4));
            CHECK(volumeOf(sky).isBaked);
            const std::optional<services::VolumetricVolumeStatus> status = fixture.service->getVolumeStatus(sky.entityHandle());
            REQUIRE(status.has_value());
            CHECK(status->baked);
            CHECK_FALSE(status->stale);
            CHECK(status->navigablePercent == doctest::Approx(100.0f));

            // Clear drops the grid and the reference; the file stays.
            events::volumetric::ClearVolumetricNavCommand clear;
            clear.volume = sky.entityHandle();
            dispatcher.execute(clear);
            CHECK(fixture.provider.getVolume(kUuid).get() == nullptr);
            CHECK_FALSE(volumeOf(sky).bakedRef.isValid());
            CHECK_FALSE(volumeOf(sky).isBaked);
            CHECK(fixture.service->getVolumeStatus(sky.entityHandle())->file.empty());
            CHECK(fs::exists(saved.path));
        }

        fs::remove_all(directory, ec);
    }

    TEST_CASE("a loaded grid turns stale when its volume moves or its settings change")
    {
        ServiceFixture fixture;
        ScopedEntity sky("Sky", glm::vec3(0.0f));
        addVolume(sky, 916806, glm::vec3(2.0f));
        REQUIRE(fixture.bake(sky.entityHandle()) == services::VolumetricBakeState::Succeeded);
        CHECK_FALSE(fixture.service->getVolumeStatus(sky.entityHandle())->stale);

        SUBCASE("moved")
        {
            sky.transform().position = glm::vec3(5.0f, 0.0f, 0.0f);
            CHECK(fixture.service->getVolumeStatus(sky.entityHandle())->stale);
            sky.transform().position = glm::vec3(0.0f);
            CHECK_FALSE(fixture.service->getVolumeStatus(sky.entityHandle())->stale);
        }

        SUBCASE("voxel size")
        {
            volumeOf(sky).voxelSize = 0.5f;
            CHECK(fixture.service->getVolumeStatus(sky.entityHandle())->stale);
        }

        SUBCASE("connectivity")
        {
            volumeOf(sky).connectivity = 6;
            CHECK(fixture.service->getVolumeStatus(sky.entityHandle())->stale);
            CHECK(fixture.service->getVolumeData(sky.entityHandle())->stale);
        }
    }

    TEST_CASE("component data writes are validated")
    {
        ServiceFixture fixture;
        auto& dispatcher = events::EventDispatcher::instance();
        ScopedEntity sky("Sky", glm::vec3(0.0f));
        addVolume(sky, 916807, glm::vec3(2.0f));

        events::volumetric::GetVolumetricNavVolumeDataQuery get;
        get.entity = sky.entityHandle();
        const std::optional<services::VolumetricNavVolumeData> data = dispatcher.query(get);
        REQUIRE(data.has_value());
        CHECK_FALSE(data->baked);
        CHECK(data->halfExtents == glm::vec3(2.0f));

        services::VolumetricNavVolumeData edit = *data;
        edit.voxelSize = 0.5f;
        edit.connectivity = 6;
        edit.agentClearance = 1.0f;
        edit.includeColliders = true;
        events::volumetric::SetVolumetricNavVolumeDataCommand set;
        set.entity = sky.entityHandle();
        set.data = edit;
        CHECK(dispatcher.execute(set));
        CHECK(volumeOf(sky).voxelSize == 0.5f);
        CHECK(volumeOf(sky).connectivity == 6);
        CHECK(volumeOf(sky).agentClearance == 1.0f);
        CHECK(volumeOf(sky).includeColliders);

        auto refused = [&](auto mutate)
        {
            services::VolumetricNavVolumeData bad = edit;
            mutate(bad);
            return !fixture.service->setVolumeData(sky.entityHandle(), bad);
        };
        CHECK(refused([](services::VolumetricNavVolumeData& d) { d.connectivity = 7; }));
        CHECK(refused([](services::VolumetricNavVolumeData& d) { d.halfExtents.y = 0.0f; }));
        CHECK(refused([](services::VolumetricNavVolumeData& d) { d.voxelSize = 0.0f; }));
        CHECK(refused([](services::VolumetricNavVolumeData& d) { d.agentClearance = -1.0f; }));
        CHECK(refused([](services::VolumetricNavVolumeData& d)
                      { d.centerOffset.x = std::numeric_limits<float>::quiet_NaN(); }));
        CHECK(volumeOf(sky).voxelSize == 0.5f); // untouched by the refused writes

        ScopedEntity flyer("Flyer", glm::vec3(0.0f));
        CHECK_FALSE(fixture.service->getAgentData(flyer.entityHandle()).has_value());
        scene::EntityRegistry::getRegistry().emplace<components::VolumetricAgentComponent>(flyer.get());
        const std::optional<services::VolumetricAgentData> agent = fixture.service->getAgentData(flyer.entityHandle());
        REQUIRE(agent.has_value());
        CHECK_FALSE(agent->registered);

        services::VolumetricAgentData agentEdit = *agent;
        agentEdit.maxSpeed = 8.0f;
        agentEdit.turnSpeed = 0.0f;
        agentEdit.alignPitch = true;
        CHECK(fixture.service->setAgentData(flyer.entityHandle(), agentEdit));
        const auto& agentComponent =
            scene::EntityRegistry::getRegistry().get<components::VolumetricAgentComponent>(flyer.get());
        CHECK(agentComponent.maxSpeed == 8.0f);
        CHECK(agentComponent.turnSpeed == 0.0f);
        CHECK(agentComponent.alignPitch);

        services::VolumetricAgentData badAgent = agentEdit;
        badAgent.radius = 0.0f;
        CHECK_FALSE(fixture.service->setAgentData(flyer.entityHandle(), badAgent));
        badAgent = agentEdit;
        badAgent.maxSpeed = -1.0f;
        CHECK_FALSE(fixture.service->setAgentData(flyer.entityHandle(), badAgent));
        badAgent = agentEdit;
        badAgent.stoppingDistance = std::numeric_limits<float>::quiet_NaN();
        CHECK_FALSE(fixture.service->setAgentData(flyer.entityHandle(), badAgent));
        CHECK(agentComponent.maxSpeed == 8.0f);
    }

    TEST_CASE("an agent destination is Moving at once in Play, ignored in Edit, and forgotten on Stop")
    {
        ServiceFixture fixture;
        auto& dispatcher = events::EventDispatcher::instance();
        ScopedEntity sky("Sky", glm::vec3(0.0f));
        addVolume(sky, 916808, glm::vec3(4.0f));
        REQUIRE(fixture.bake(sky.entityHandle()) == services::VolumetricBakeState::Succeeded);

        ScopedEntity flyer("Flyer", glm::vec3(-2.5f, 0.5f, 0.5f));
        scene::EntityRegistry::getRegistry().emplace<components::VolumetricAgentComponent>(flyer.get());

        int reached = 0;
        events::ScopedSubscription reachedSubscription(
            dispatcher.subscribe<events::volumetric::VolumetricAgentReachedDestinationNotification>(
                [&reached](const events::volumetric::VolumetricAgentReachedDestinationNotification&) { ++reached; }));

        events::volumetric::SetVolumetricAgentDestinationCommand move;
        move.entity = flyer.entityHandle();
        move.target = glm::vec3(2.5f, 0.5f, 0.5f);
        events::volumetric::GetVolumetricAgentStateQuery stateQuery;
        stateQuery.entity = flyer.entityHandle();
        auto state = [&]() { return dispatcher.query(stateQuery).value_or(services::VolumetricAgentState{}).state; };

        // Edit mode: no flying agents.
        dispatcher.execute(move);
        REQUIRE(dispatcher.query(stateQuery).has_value());
        CHECK(state() == services::VolumetricAgentMoveState::Idle);
        CHECK_FALSE(fixture.service->agents().isRegistered(flyer.entityHandle()));

        ServiceFixture::setPlayMode(true);
        dispatcher.execute(move);
        CHECK(state() == services::VolumetricAgentMoveState::Moving); // synchronously, inside the command

        for (int i = 0; i < 400 && state() == services::VolumetricAgentMoveState::Moving; ++i)
            fixture.service->update(0.05f, true);
        CHECK(state() == services::VolumetricAgentMoveState::Arrived);
        CHECK(reached == 1);
        CHECK(glm::distance(flyer.worldPosition(), move.target) <= 0.5f + 1.0e-3f);

        // A new destination is Moving at once; without simulate (paused) the agent stays where it is.
        move.target = glm::vec3(-2.5f, 0.5f, 0.5f);
        dispatcher.execute(move);
        CHECK(state() == services::VolumetricAgentMoveState::Moving);
        const glm::vec3 parked = flyer.worldPosition();
        fixture.service->update(0.05f, false);
        CHECK(flyer.worldPosition() == parked);

        events::volumetric::StopVolumetricAgentCommand stop;
        stop.entity = flyer.entityHandle();
        dispatcher.execute(stop);
        CHECK(state() == services::VolumetricAgentMoveState::Idle);
        CHECK(fixture.service->agents().isRegistered(flyer.entityHandle()));

        // Play -> Edit forgets the agents.
        ServiceFixture::setPlayMode(false);
        CHECK_FALSE(fixture.service->agents().isRegistered(flyer.entityHandle()));
        CHECK(state() == services::VolumetricAgentMoveState::Idle);
        CHECK_FALSE(scene::EntityRegistry::getRegistry().get<components::VolumetricAgentComponent>(flyer.get()).isActive);
    }

    TEST_CASE("the volumetric snapshot takes solid colliders as triangles, spheres and capsules")
    {
        // Far from the geometry other suites leave in the global registry.
        const glm::vec3 site{7000.0f, 50.0f, 7000.0f};
        auto& registry = scene::EntityRegistry::getRegistry();

        ScopedEntity box("Box", site);
        registry.emplace<components::ColliderComponent>(box.get()).shape = components::ColliderShape::Box;

        ScopedEntity ball("Ball", site + glm::vec3(5.0f, 0.0f, 0.0f));
        {
            auto& collider = registry.emplace<components::ColliderComponent>(ball.get());
            collider.shape = components::ColliderShape::Sphere;
            collider.size = glm::vec3(1.5f);
            collider.offset = glm::vec3(0.0f, 1.0f, 0.0f);
        }

        ScopedEntity pill("Pill", site + glm::vec3(-5.0f, 0.0f, 0.0f));
        {
            auto& collider = registry.emplace<components::ColliderComponent>(pill.get());
            collider.shape = components::ColliderShape::Capsule;
            collider.size = glm::vec3(0.5f);
            collider.height = 3.0f; // total: a 2 m segment between the caps
        }

        ScopedEntity trigger("Trigger", site);
        {
            auto& collider = registry.emplace<components::ColliderComponent>(trigger.get());
            collider.shape = components::ColliderShape::Sphere;
            collider.isTrigger = true;
        }

        // A mover never blocks the space it flies through.
        ScopedEntity flyer("Flyer", site);
        registry.emplace<components::ColliderComponent>(flyer.get()).shape = components::ColliderShape::Sphere;
        registry.emplace<components::VolumetricAgentComponent>(flyer.get());

        services::SceneGeometrySources sources;
        sources.terrain = false;
        sources.staticMeshes = false;
        sources.foliage = false;
        sources.colliders = true;
        sources.obstacles = false;
        volumetric::VolumetricBakeInput input;
        services::SceneGeometryCollector(sources).collectVolumetric(site - glm::vec3(20.0f), site + glm::vec3(20.0f),
                                                                    input);

        CHECK(input.terrainVertices.empty());
        CHECK_FALSE(input.vertices.empty()); // the box collider's triangles
        CHECK(input.indices.size() % 3 == 0);
        REQUIRE(input.spheres.size() == 1);
        CHECK(glm::distance(input.spheres[0].center, site + glm::vec3(5.0f, 1.0f, 0.0f)) < 1.0e-3f);
        CHECK(input.spheres[0].radius == doctest::Approx(1.5f));
        REQUIRE(input.capsules.size() == 1);
        CHECK(input.capsules[0].radius == doctest::Approx(0.5f));
        CHECK(glm::distance(input.capsules[0].a, site + glm::vec3(-5.0f, -1.0f, 0.0f)) < 1.0e-3f);
        CHECK(glm::distance(input.capsules[0].b, site + glm::vec3(-5.0f, 1.0f, 0.0f)) < 1.0e-3f);
    }

    TEST_CASE("a shown path is drawn every frame until its seconds run out, whatever the overlay toggle")
    {
        std::vector<std::pair<glm::vec3, glm::vec3>> lines; // outlives the fixture, which clears the handler
        ServiceFixture fixture;
        auto& dispatcher = events::EventDispatcher::instance();
        dispatcher.registerCommandHandler<events::debugdraw::DrawLineCommand>(
            [&lines](const events::debugdraw::DrawLineCommand& command) { lines.emplace_back(command.start, command.end); });

        events::volumetric::ShowVolumetricPathCommand show;
        show.waypoints = {glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(4.0f, 1.0f, 0.0f), glm::vec3(4.0f, 3.0f, 2.0f)};
        show.snaps = {{glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f)}};
        show.seconds = 1.0f;
        dispatcher.execute(show);

        // GetShowNavmeshDebugQuery has no handler here, so the volumetric overlay itself is off.
        fixture.service->update(0.25f, false);
        REQUIRE(lines.size() == 3); // the two path segments, then the snap
        CHECK(glm::distance(lines[0].first, glm::vec3(0.0f, 1.0f, 0.0f)) < 1.0e-6f);
        CHECK(glm::distance(lines[1].second, glm::vec3(4.0f, 3.0f, 2.0f)) < 1.0e-6f);
        CHECK(glm::distance(lines[2].first, glm::vec3(0.0f, 0.0f, 0.0f)) < 1.0e-6f);
        CHECK(glm::distance(lines[2].second, glm::vec3(0.0f, 1.0f, 0.0f)) < 1.0e-6f);

        lines.clear();
        fixture.service->update(0.5f, false); // 0.25 s left
        CHECK(lines.size() == 3);

        lines.clear();
        fixture.service->update(0.5f, false); // its time is up
        CHECK(lines.empty());
        fixture.service->update(0.5f, false);
        CHECK(lines.empty());

        SUBCASE("a new path replaces the drawn one")
        {
            dispatcher.execute(show);
            show.waypoints = {glm::vec3(10.0f, 0.0f, 0.0f), glm::vec3(11.0f, 0.0f, 0.0f)};
            show.snaps.clear();
            dispatcher.execute(show);
            fixture.service->update(0.1f, false);
            REQUIRE(lines.size() == 1);
            CHECK(glm::distance(lines[0].first, glm::vec3(10.0f, 0.0f, 0.0f)) < 1.0e-6f);
        }
        SUBCASE("an empty path or no time clears it")
        {
            dispatcher.execute(show);
            events::volumetric::ShowVolumetricPathCommand empty;
            dispatcher.execute(empty);
            fixture.service->update(0.1f, false);
            CHECK(lines.empty());

            dispatcher.execute(show);
            show.seconds = 0.0f;
            dispatcher.execute(show);
            fixture.service->update(0.1f, false);
            CHECK(lines.empty());
        }
    }
}
