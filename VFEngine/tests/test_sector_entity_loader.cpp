#include <doctest.h>
#include <world/SectorEntityLoader.hpp>
#include <scene/SceneGraphSystem.hpp>
#include <scene/Entity.hpp>
#include <scene/EntityRegistry.hpp>
#include <serialization/SceneSerialization.hpp>
#include <components/Components.hpp>
#include <nlohmann/json.hpp>

#include <string>
#include <utility>
#include <vector>

// ============================================================
// SectorEntityLoader (deferred spawn/destroy with per-frame
// budget + lifecycle callbacks).
// PendingReferenceResolver moved to test_pending_reference_resolver.cpp (VK-1590).
// ============================================================

namespace
{
    using json = nlohmann::json;

    // Serialize a throwaway entity into the JSON shape sector files store,
    // then destroy it so the loader is the one bringing it back.
    std::pair<std::string, json> makeEntityPayload(const std::string& name,
                                                   uint64_t uuid,
                                                   const glm::vec3& position)
    {
        scene::EntityRegistry::init(); // connect UUID lookup hooks (idempotent)
        scene::Entity entity(name);
        entity.addOrReplaceComponent<components::UUIDComponent>(uuid);
        auto& transform = entity.getComponent<components::TransformComponent>();
        transform.position = position;

        json entityJson = serialization::SceneSerialization::serializeEntity(entity);
        scene::EntityRegistry::getRegistry().destroy(entity.getHandle());
        return {name, entityJson};
    }

    bool entityExists(uint64_t uuid)
    {
        return scene::EntityRegistry::findByUUID(uuid) != entt::null;
    }

    // Destroy any test entities the loader spawned, via the loader's own unload path
    void unloadAll(world::SectorEntityLoader& loader, scene::SceneGraphSystem& sceneGraph,
                   const world::SectorCoord& coord, const std::vector<uint64_t>& uuids)
    {
        std::vector<uint64_t> alive;
        for (uint64_t uuid : uuids)
        {
            if (entityExists(uuid))
                alive.push_back(uuid);
        }
        loader.queueSectorUnload(coord, alive);
        loader.flush(sceneGraph);
    }
}

TEST_SUITE("SectorEntityLoader")
{
    TEST_CASE("maxEntitiesPerFrame batches spawns across updates")
    {
        scene::SceneGraphSystem sceneGraph;
        world::SectorEntityLoader loader;
        world::SectorCoord coord{0, 0};

        std::vector<uint64_t> uuids{910001, 910002, 910003, 910004, 910005};
        std::vector<std::pair<std::string, json>> payload;
        for (size_t i = 0; i < uuids.size(); ++i)
            payload.push_back(makeEntityPayload("Batch" + std::to_string(i), uuids[i],
                                                glm::vec3(static_cast<float>(i), 0.0f, 0.0f)));

        loader.queueSectorLoadFromData(coord, payload);

        loader.update(sceneGraph, 2);
        int spawned = 0;
        for (uint64_t uuid : uuids)
            spawned += entityExists(uuid) ? 1 : 0;
        CHECK(spawned == 2);
        CHECK(loader.hasPendingLoadsForSector(coord));

        loader.update(sceneGraph, 2);
        spawned = 0;
        for (uint64_t uuid : uuids)
            spawned += entityExists(uuid) ? 1 : 0;
        CHECK(spawned == 4);

        loader.flush(sceneGraph);
        for (uint64_t uuid : uuids)
            CHECK(entityExists(uuid));
        CHECK_FALSE(loader.hasPendingLoadsForSector(coord));

        unloadAll(loader, sceneGraph, coord, uuids);
    }

    TEST_CASE("deserialized transform survives the loader round-trip")
    {
        scene::SceneGraphSystem sceneGraph;
        world::SectorEntityLoader loader;
        world::SectorCoord coord{0, 0};

        glm::vec3 position{12.5f, -3.0f, 40.0f};
        std::vector<std::pair<std::string, json>> payload;
        payload.push_back(makeEntityPayload("Transformed", 910100, position));

        loader.queueSectorLoadFromData(coord, payload);
        loader.flush(sceneGraph);

        REQUIRE(entityExists(910100));
        scene::Entity entity(scene::EntityRegistry::findByUUID(910100));
        const auto& transform = entity.getComponent<components::TransformComponent>();
        CHECK(transform.position.x == doctest::Approx(position.x));
        CHECK(transform.position.y == doctest::Approx(position.y));
        CHECK(transform.position.z == doctest::Approx(position.z));

        unloadAll(loader, sceneGraph, coord, {910100});
    }

    TEST_CASE("duplicate UUID payloads spawn a single entity")
    {
        scene::SceneGraphSystem sceneGraph;
        world::SectorEntityLoader loader;
        world::SectorCoord coord{0, 0};

        auto payloadEntry = makeEntityPayload("Dup", 910200, glm::vec3(0.0f));

        SUBCASE("queued twice in the same batch")
        {
            std::vector<std::pair<std::string, json>> payload;
            payload.push_back(payloadEntry);
            payload.push_back({payloadEntry.first, payloadEntry.second});
            loader.queueSectorLoadFromData(coord, payload);
            loader.flush(sceneGraph);
        }

        SUBCASE("re-queued after already spawned")
        {
            std::vector<std::pair<std::string, json>> payload;
            payload.push_back({payloadEntry.first, payloadEntry.second});
            loader.queueSectorLoadFromData(coord, payload);
            loader.flush(sceneGraph);

            std::vector<std::pair<std::string, json>> again;
            again.push_back({payloadEntry.first, payloadEntry.second});
            loader.queueSectorLoadFromData(coord, again);
            loader.flush(sceneGraph);
        }

        auto& registry = scene::EntityRegistry::getRegistry();
        int matches = 0;
        for (auto [entity, uuidComp] : registry.view<components::UUIDComponent>().each())
        {
            if (uuidComp.id.getValue() == 910200)
                ++matches;
        }
        CHECK(matches == 1);

        unloadAll(loader, sceneGraph, coord, {910200});
    }

    TEST_CASE("lifecycle callbacks fire in order")
    {
        scene::SceneGraphSystem sceneGraph;
        world::SectorEntityLoader loader;
        world::SectorCoord coord{2, 3};

        std::vector<std::string> events;
        loader.setOnEntityPostLoad([&](uint64_t, const std::string&, const std::string&)
                                   { events.push_back("postLoad"); });
        loader.setOnEntityLoaded([&](uint64_t uuid, const world::SectorCoord& c)
        {
            events.push_back("loaded");
            CHECK(uuid == 910300);
            CHECK(c == coord);
        });
        loader.setOnEntityPreDestroy([&](uint64_t) { events.push_back("preDestroy"); });
        loader.setOnEntityUnloaded([&](uint64_t uuid, const world::SectorCoord& c)
        {
            events.push_back("unloaded");
            CHECK(uuid == 910300);
            CHECK(c == coord);
        });

        std::vector<std::pair<std::string, json>> payload;
        payload.push_back(makeEntityPayload("Callbacks", 910300, glm::vec3(0.0f)));
        loader.queueSectorLoadFromData(coord, payload);
        loader.flush(sceneGraph);

        REQUIRE(events.size() == 2);
        CHECK(events[0] == "postLoad");
        CHECK(events[1] == "loaded");

        loader.queueSectorUnload(coord, {910300});
        loader.flush(sceneGraph);

        REQUIRE(events.size() == 4);
        CHECK(events[2] == "preDestroy");
        CHECK(events[3] == "unloaded");
        CHECK_FALSE(entityExists(910300));
    }

    TEST_CASE("pending unloads consume the frame budget before loads")
    {
        scene::SceneGraphSystem sceneGraph;
        world::SectorEntityLoader loader;
        world::SectorCoord coord{0, 0};

        // Spawn entity A first
        std::vector<std::pair<std::string, json>> payloadA;
        payloadA.push_back(makeEntityPayload("First", 910400, glm::vec3(0.0f)));
        loader.queueSectorLoadFromData(coord, payloadA);
        loader.flush(sceneGraph);
        REQUIRE(entityExists(910400));

        // Queue A's unload and B's load; budget of 1 only processes the unload
        loader.queueSectorUnload(coord, {910400});
        std::vector<std::pair<std::string, json>> payloadB;
        payloadB.push_back(makeEntityPayload("Second", 910401, glm::vec3(0.0f)));
        loader.queueSectorLoadFromData(coord, payloadB);

        loader.update(sceneGraph, 1);
        CHECK_FALSE(entityExists(910400));
        CHECK_FALSE(entityExists(910401));
        CHECK(loader.hasPendingLoadsForSector(coord));

        loader.flush(sceneGraph);
        CHECK(entityExists(910401));

        unloadAll(loader, sceneGraph, coord, {910401});
    }

    TEST_CASE("getLoadProgress tracks queued vs processed entities")
    {
        scene::SceneGraphSystem sceneGraph;
        world::SectorEntityLoader loader;
        world::SectorCoord coord{0, 0};

        // Idle loader reports complete (nothing queued).
        CHECK(loader.getLoadProgress().entitiesQueued == 0);
        CHECK(loader.getLoadProgress().pending() == 0);
        CHECK(loader.getLoadProgress().fraction() == doctest::Approx(1.0f));

        std::vector<uint64_t> uuids{910600, 910601, 910602, 910603};
        std::vector<std::pair<std::string, json>> payload;
        for (size_t i = 0; i < uuids.size(); ++i)
            payload.push_back(makeEntityPayload("Prog" + std::to_string(i), uuids[i],
                                                glm::vec3(static_cast<float>(i), 0.0f, 0.0f)));
        loader.queueSectorLoadFromData(coord, payload);

        auto p = loader.getLoadProgress();
        CHECK(p.entitiesQueued == 4);
        CHECK(p.entitiesLoaded == 0);
        CHECK(p.pending() == 4);
        CHECK(p.fraction() == doctest::Approx(0.0f));

        loader.update(sceneGraph, 2);
        p = loader.getLoadProgress();
        CHECK(p.entitiesLoaded == 2);
        CHECK(p.pending() == 2);
        CHECK(p.fraction() == doctest::Approx(0.5f));

        loader.flush(sceneGraph);
        p = loader.getLoadProgress();
        CHECK(p.entitiesLoaded == 4);
        CHECK(p.pending() == 0);
        CHECK(p.fraction() == doctest::Approx(1.0f));

        unloadAll(loader, sceneGraph, coord, uuids);
    }

    TEST_CASE("cancelled loads do not strand progress below 1.0")
    {
        scene::SceneGraphSystem sceneGraph;
        world::SectorEntityLoader loader;
        world::SectorCoord keep{1, 0};
        world::SectorCoord cancel{0, 0};

        std::vector<std::pair<std::string, json>> cancelPayload;
        cancelPayload.push_back(makeEntityPayload("CancelledProg", 910700, glm::vec3(0.0f)));
        cancelPayload.push_back(makeEntityPayload("CancelledProg2", 910701, glm::vec3(0.0f)));
        loader.queueSectorLoadFromData(cancel, cancelPayload);

        std::vector<std::pair<std::string, json>> keepPayload;
        keepPayload.push_back(makeEntityPayload("KeptProg", 910702, glm::vec3(0.0f)));
        loader.queueSectorLoadFromData(keep, keepPayload);

        CHECK(loader.getLoadProgress().entitiesQueued == 3);

        loader.cancelPendingLoads(cancel);
        // Two cancelled entities drop out of the queued total.
        CHECK(loader.getLoadProgress().entitiesQueued == 1);

        loader.flush(sceneGraph);
        auto p = loader.getLoadProgress();
        CHECK(p.entitiesLoaded == 1);
        CHECK(p.pending() == 0);
        CHECK(p.fraction() == doctest::Approx(1.0f));

        unloadAll(loader, sceneGraph, keep, {910702});
    }

    TEST_CASE("cancelPendingLoads drops only the cancelled sector")
    {
        scene::SceneGraphSystem sceneGraph;
        world::SectorEntityLoader loader;
        world::SectorCoord keep{1, 0};
        world::SectorCoord cancel{0, 0};

        std::vector<std::pair<std::string, json>> cancelPayload;
        cancelPayload.push_back(makeEntityPayload("Cancelled", 910500, glm::vec3(0.0f)));
        loader.queueSectorLoadFromData(cancel, cancelPayload);

        std::vector<std::pair<std::string, json>> keepPayload;
        keepPayload.push_back(makeEntityPayload("Kept", 910501, glm::vec3(0.0f)));
        loader.queueSectorLoadFromData(keep, keepPayload);

        loader.cancelPendingLoads(cancel);
        CHECK_FALSE(loader.hasPendingLoadsForSector(cancel));
        CHECK(loader.hasPendingLoadsForSector(keep));

        loader.flush(sceneGraph);
        CHECK_FALSE(entityExists(910500));
        CHECK(entityExists(910501));

        unloadAll(loader, sceneGraph, keep, {910501});
    }
}
