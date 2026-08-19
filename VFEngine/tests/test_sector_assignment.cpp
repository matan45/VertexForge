#include <doctest.h>
#include <world/SectorAssignment.hpp>
#include <world/WorldSector.hpp>
#include <world/WorldSectorManager.hpp>
#include <world/WorldTypes.hpp>
#include <scene/Entity.hpp>
#include <scene/EntityRegistry.hpp>
#include <scene/SceneGraphSystem.hpp>
#include <components/Components.hpp>
#include <components/ComponentClone.hpp>
#include <serialization/SceneSerialization.hpp>
#include <serialization/PrefabSerialization.hpp>
#include <terrain/TerrainTypes.hpp>
#include <nlohmann/json.hpp>

#include <cmath>
#include <limits>

// ============================================================
// VK-1597 — per-entity streaming policy.
//
// Covers the pure bucketing decision extracted out of the two anonymous-namespace
// isManagedBySeparateSystem() copies, its parity with the coord math it was lifted from, the
// data-loss guard on migration, and the component's round-trip through the tables that would
// otherwise drop it silently.
// ============================================================

namespace
{
    world::SectorConfig makeConfig(float sectorWorldSize = 100.0f)
    {
        world::SectorConfig config;
        config.sectorWorldSize = sectorWorldSize;
        return config;
    }

    world::EntityStreamingTraits spatialTraitsAt(const glm::vec3& position)
    {
        world::EntityStreamingTraits traits;
        traits.hasTransform = true;
        traits.position = position;
        return traits;
    }

    // Real registry entity. EntityRegistry::init() connects the UUID lookup hooks that only the
    // Editor/Runtime bootstraps normally install; it is idempotent.
    scene::Entity makeEntity(const std::string& name)
    {
        scene::EntityRegistry::init();
        return scene::Entity(name);
    }
}

TEST_SUITE("SectorAssignment")
{
    // ---- the pure decision -------------------------------------------------------------

    TEST_CASE("plain entity buckets into the sector under its position")
    {
        const auto config = makeConfig();

        auto result = world::resolveSectorAssignment(spatialTraitsAt({150.0f, 12.0f, 250.0f}), config);

        CHECK(result.kind == world::SectorAssignmentKind::Spatial);
        CHECK(result.isSpatial());
        CHECK(result.coord == world::SectorCoord(1, 2));
    }

    TEST_CASE("spatiallyLoaded == false yields no sector")
    {
        const auto config = makeConfig();

        auto traits = spatialTraitsAt({150.0f, 0.0f, 250.0f});
        traits.spatiallyLoaded = false;

        auto result = world::resolveSectorAssignment(traits, config);

        CHECK(result.kind == world::SectorAssignmentKind::NotSpatiallyLoaded);
        CHECK_FALSE(result.isSpatial());
        // The coord is left default rather than computed - callers must gate on isSpatial().
        CHECK(result.coord == world::SectorCoord(0, 0));
    }

    TEST_CASE("the type skip-list still wins, and reports itself as the reason")
    {
        const auto config = makeConfig();

        auto traits = spatialTraitsAt({150.0f, 0.0f, 250.0f});
        traits.managedBySeparateSystem = true;

        CHECK(world::resolveSectorAssignment(traits, config).kind
              == world::SectorAssignmentKind::ManagedBySeparateSystem);

        // Both refusals at once still reports the skip-list, so the diagnostic never changes for
        // an entity that was already excluded before VK-1597 existed.
        traits.spatiallyLoaded = false;
        CHECK(world::resolveSectorAssignment(traits, config).kind
              == world::SectorAssignmentKind::ManagedBySeparateSystem);
    }

    TEST_CASE("an entity with no transform is not bucketed")
    {
        world::EntityStreamingTraits traits; // hasTransform defaults to false
        CHECK(world::resolveSectorAssignment(traits, makeConfig()).kind
              == world::SectorAssignmentKind::NoTransform);
    }

    // ---- boundary behaviour, and parity with the manager it was lifted from -------------

    TEST_CASE("boundary positions floor toward negative infinity on both axes")
    {
        const auto config = makeConfig(100.0f);

        // Exactly on a boundary belongs to the higher sector.
        CHECK(world::worldPositionToSectorCoord({100.0f, 0.0f, 0.0f}, config) == world::SectorCoord(1, 0));
        CHECK(world::worldPositionToSectorCoord({0.0f, 0.0f, 100.0f}, config) == world::SectorCoord(0, 1));

        // Just under a boundary stays in the lower one.
        CHECK(world::worldPositionToSectorCoord({99.999f, 0.0f, 0.0f}, config) == world::SectorCoord(0, 0));

        // Negative coordinates floor, they do not truncate toward zero.
        CHECK(world::worldPositionToSectorCoord({-0.001f, 0.0f, -0.001f}, config) == world::SectorCoord(-1, -1));
        CHECK(world::worldPositionToSectorCoord({-100.0f, 0.0f, -100.0f}, config) == world::SectorCoord(-1, -1));
        CHECK(world::worldPositionToSectorCoord({-100.001f, 0.0f, 0.0f}, config) == world::SectorCoord(-2, 0));

        // -0.0f is still sector 0, not -1.
        CHECK(world::worldPositionToSectorCoord({-0.0f, 0.0f, -0.0f}, config) == world::SectorCoord(0, 0));

        // Y is ignored entirely - the grid is 2D.
        CHECK(world::worldPositionToSectorCoord({50.0f, 99999.0f, 50.0f}, config) == world::SectorCoord(0, 0));
    }

    TEST_CASE("VK-1588 float-space clamp survived the move out of WorldSectorManager")
    {
        const auto config = makeConfig(100.0f);
        const float inf = std::numeric_limits<float>::infinity();
        const float nan = std::numeric_limits<float>::quiet_NaN();

        CHECK(world::worldPositionToSectorCoord({nan, 0.0f, nan}, config) == world::SectorCoord(0, 0));
        CHECK(world::worldPositionToSectorCoord({inf, 0.0f, -inf}, config) == world::SectorCoord(0, 0));

        // A zero sectorWorldSize divides into inf/NaN rather than garbage, and collapses to 0.
        CHECK(world::worldPositionToSectorCoord({500.0f, 0.0f, 500.0f}, makeConfig(0.0f))
              == world::SectorCoord(0, 0));

        // Far outside the addressable range, the clamp happens BEFORE the int cast.
        const float huge = 1.0e30f;
        auto clamped = world::worldPositionToSectorCoord({huge, 0.0f, -huge}, config);
        CHECK(clamped.x == world::kMaxSectorCoord);
        CHECK(clamped.z == world::kMinSectorCoord);
    }

    TEST_CASE("WorldSectorManager delegates to the same coord math")
    {
        const auto config = makeConfig(128.0f);
        world::WorldSectorManager manager(config);

        const glm::vec3 samples[] = {
            {0.0f, 0.0f, 0.0f},        {127.9f, 0.0f, 127.9f},   {128.0f, 0.0f, 128.0f},
            {-1.0f, 0.0f, -1.0f},      {-128.0f, 0.0f, -256.0f}, {5000.0f, 0.0f, -5000.0f},
        };

        for (const auto& p : samples)
        {
            CHECK(manager.worldPositionToSectorCoord(p) == world::worldPositionToSectorCoord(p, config));
        }

        // Same for the tile-coord overload.
        const terrain::TileCoord tile(3, -2);
        CHECK(manager.tileCoordToSectorCoord(tile, 32.0f)
              == world::tileOriginToSectorCoord(tile.x, tile.z, 32.0f, config));
    }

    TEST_CASE("assignEntityToSector(coord) matches the position overload")
    {
        const auto config = makeConfig(100.0f);
        world::WorldSectorManager byPosition(config);
        world::WorldSectorManager byCoord(config);

        const glm::vec3 pos(150.0f, 0.0f, -50.0f);
        byPosition.assignEntityToSector(970001, pos);
        byCoord.assignEntityToSector(970001, world::worldPositionToSectorCoord(pos, config));

        CHECK(byPosition.getEntitySector(970001) == byCoord.getEntitySector(970001));
        CHECK(byCoord.getEntitySector(970001) == world::SectorCoord(1, -1));
        REQUIRE(byCoord.getSector({1, -1}) != nullptr);
        CHECK(byCoord.getSector({1, -1})->dirty);
    }

    // ---- the migration data-loss guard ------------------------------------------------

    TEST_CASE("canMigrateEntity accepts only Loaded sectors")
    {
        world::WorldSector sector;

        sector.state = world::SectorState::Loaded;
        CHECK(world::canMigrateEntity(sector));

        // Every other state has a non-authoritative entityUUIDs, so dirtying it would make the
        // next Save World overwrite a good .vfsector with an empty one.
        for (auto state : {world::SectorState::Unloaded, world::SectorState::Loading,
                           world::SectorState::Unloading, world::SectorState::Prefetching,
                           world::SectorState::Prefetched})
        {
            sector.state = state;
            CHECK_FALSE(world::canMigrateEntity(sector));
        }
    }

    // ---- reading traits off a live entity ----------------------------------------------

    TEST_CASE("readEntityStreamingTraits: absent component means spatially loaded")
    {
        auto entity = makeEntity("PlainEntity");
        entity.getComponent<components::TransformComponent>().position = {250.0f, 0.0f, 50.0f};

        const auto traits = world::readEntityStreamingTraits(entity);

        CHECK(traits.spatiallyLoaded);
        CHECK_FALSE(traits.managedBySeparateSystem);
        CHECK(traits.hasTransform);
        CHECK(traits.position.x == doctest::Approx(250.0f));

        CHECK(world::resolveEntitySectorAssignment(entity, makeConfig()).coord == world::SectorCoord(2, 0));
    }

    TEST_CASE("readEntityStreamingTraits: the pin is honoured")
    {
        auto entity = makeEntity("PinnedEntity");
        entity.addComponent<components::StreamingPolicyComponent>().spatiallyLoaded = false;

        CHECK_FALSE(world::readEntityStreamingTraits(entity).spatiallyLoaded);
        CHECK(world::resolveEntitySectorAssignment(entity, makeConfig()).kind
              == world::SectorAssignmentKind::NotSpatiallyLoaded);
    }

    TEST_CASE("readEntityStreamingTraits: every skip-list type is still excluded")
    {
        const auto config = makeConfig();

        {
            auto e = makeEntity("Terrain");
            e.addComponent<components::TerrainComponent>();
            CHECK(world::resolveEntitySectorAssignment(e, config).kind
                  == world::SectorAssignmentKind::ManagedBySeparateSystem);
        }
        {
            auto e = makeEntity("TerrainTile");
            e.addComponent<components::TerrainTileComponent>();
            CHECK(world::resolveEntitySectorAssignment(e, config).kind
                  == world::SectorAssignmentKind::ManagedBySeparateSystem);
        }
        {
            auto e = makeEntity("Ocean");
            e.addComponent<components::OceanComponent>();
            CHECK(world::resolveEntitySectorAssignment(e, config).kind
                  == world::SectorAssignmentKind::ManagedBySeparateSystem);
        }
        {
            auto e = makeEntity("IBL");
            e.addComponent<components::IBLComponent>();
            CHECK(world::resolveEntitySectorAssignment(e, config).kind
                  == world::SectorAssignmentKind::ManagedBySeparateSystem);
        }
        {
            auto e = makeEntity("Camera");
            e.addComponent<components::CameraComponent>();
            CHECK(world::resolveEntitySectorAssignment(e, config).kind
                  == world::SectorAssignmentKind::ManagedBySeparateSystem);
        }
    }

    // ---- the tables that lose the flag silently ----------------------------------------

    TEST_CASE("StreamingPolicyComponent survives scene serialization")
    {
        // This is also what covers .vfsector: WorldSectorSerialization and SectorEntityLoader
        // reuse serializeEntity/deserializeEntity rather than keeping a component table of
        // their own.
        auto source = makeEntity("PinnedForSave");
        source.addComponent<components::StreamingPolicyComponent>().spatiallyLoaded = false;

        const nlohmann::json payload = serialization::SceneSerialization::serializeEntity(source);
        REQUIRE(payload.contains("components"));
        REQUIRE(payload["components"].contains("streamingPolicy"));
        CHECK_FALSE(payload["components"]["streamingPolicy"]["spatiallyLoaded"].get<bool>());

        scene::SceneGraphSystem sceneGraph;
        auto restored = makeEntity("Restored");
        size_t entitiesLoaded = 0;
        // isRoot = false, so the payload's uuid is NOT re-applied and the two entities stay
        // distinct in the registry's UUID map.
        serialization::DeserializeEntityContext ctx{sceneGraph, false, nullptr, entitiesLoaded, 1, {}};
        serialization::SceneSerialization::deserializeEntity(payload, restored, ctx);

        REQUIRE(restored.hasComponent<components::StreamingPolicyComponent>());
        CHECK_FALSE(restored.getComponent<components::StreamingPolicyComponent>().spatiallyLoaded);
    }

    TEST_CASE("an unpinned entity writes no streamingPolicy key")
    {
        auto source = makeEntity("NeverPinned");

        const nlohmann::json payload = serialization::SceneSerialization::serializeEntity(source);
        REQUIRE(payload.contains("components"));
        CHECK_FALSE(payload["components"].contains("streamingPolicy"));
    }

    TEST_CASE("StreamingPolicyComponent survives the prefab entity-tree round-trip")
    {
        // PrefabSerialization keeps its OWN component table, so this is a separate failure mode
        // from the scene one above: a prefab of an always-loaded landmark must instantiate pinned.
        auto source = makeEntity("PinnedPrefabSource");
        source.addComponent<components::StreamingPolicyComponent>().spatiallyLoaded = false;

        const nlohmann::json tree = serialization::PrefabSerialization::serializeEntityTree(source);
        REQUIRE(tree.contains("components"));
        REQUIRE(tree["components"].contains("streamingPolicy"));
        CHECK_FALSE(tree["components"]["streamingPolicy"]["spatiallyLoaded"].get<bool>());

        // Parent must be a real scene-graph node, not a detached entity - deserializeEntityTree
        // parents the instance through the graph (see test_scene_hierarchy_reorder.cpp:136-137).
        scene::SceneGraphSystem sceneGraph;
        auto& parent = sceneGraph.GetRoot();
        auto instance = serialization::PrefabSerialization::deserializeEntityTree(tree, parent, sceneGraph);

        REQUIRE(instance.isValid());
        REQUIRE(instance.hasComponent<components::StreamingPolicyComponent>());
        CHECK_FALSE(instance.getComponent<components::StreamingPolicyComponent>().spatiallyLoaded);
    }

    TEST_CASE("duplicating a pinned entity keeps the pin")
    {
        // The exact gotcha VK-1597 calls out: cloneOptionalComponents folds over the
        // OptionalComponents type_list, so a component missing from it is dropped silently.
        auto source = makeEntity("PinnedOriginal");
        source.addComponent<components::StreamingPolicyComponent>().spatiallyLoaded = false;

        auto copy = makeEntity("PinnedCopy");
        components::cloneOptionalComponents(source, copy);

        REQUIRE(copy.hasComponent<components::StreamingPolicyComponent>());
        CHECK_FALSE(copy.getComponent<components::StreamingPolicyComponent>().spatiallyLoaded);
    }
}
