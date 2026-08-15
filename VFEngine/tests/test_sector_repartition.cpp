#include <doctest.h>
#include <world/SectorRepartitionPlanner.hpp>
#include <world/SectorRepartitionTypes.hpp>
#include <world/SectorAssignment.hpp>
#include <world/SectorEntityLoader.hpp>
#include <world/WorldSector.hpp>
#include <world/WorldSectorManager.hpp>
#include <world/WorldSectorSerialization.hpp>
#include <world/WorldTypes.hpp>
#include <scene/Entity.hpp>
#include <scene/EntityRegistry.hpp>
#include <scene/SceneGraphSystem.hpp>
#include <components/Components.hpp>
#include <serialization/SceneSerialization.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

// ============================================================
// VK-1598 - world re-partition / cell-size migration.
//
// The migration is a COLD, file-to-file transform: entity nodes are moved between sector arrays
// without ever being deserialized. These cases pin the three things that makes load-bearing -
// that the JSON-side trait reader agrees with the registry-side one, that a round trip preserves
// UUIDs and transforms exactly, and that nothing is ever silently dropped.
// ============================================================

namespace
{
    namespace fs = std::filesystem;
    using json = nlohmann::json;

    world::SectorConfig makeConfig(float sectorWorldSize, int32_t tilesPerSector = 4)
    {
        world::SectorConfig config;
        config.sectorWorldSize = sectorWorldSize;
        config.tilesPerSector = tilesPerSector;
        return config;
    }

    // EntityRegistry::init() connects the UUID lookup hooks that only the Editor/Runtime bootstraps
    // normally install; it is idempotent.
    scene::Entity makeEntity(const std::string& name)
    {
        scene::EntityRegistry::init();
        return scene::Entity(name);
    }

    // A serialized node in exactly the shape a .vfsector stores, produced by the real serializer so
    // the trait reader under test can never be pinned against a hand-written approximation of it.
    json serializeThrowaway(scene::Entity& entity)
    {
        json node = serialization::SceneSerialization::serializeEntity(entity);
        scene::EntityRegistry::getRegistry().destroy(entity.getHandle());
        return node;
    }

    json entityNode(const std::string& name, uint64_t uuid, const glm::vec3& position)
    {
        auto entity = makeEntity(name);
        entity.addOrReplaceComponent<components::UUIDComponent>(uuid);
        entity.getComponent<components::TransformComponent>().position = position;
        return serializeThrowaway(entity);
    }

    world::RepartitionSourceSector sourceSector(const world::SectorCoord& coord,
                                                std::vector<json> entities)
    {
        world::RepartitionSourceSector source;
        source.coord = coord;
        source.entities = std::move(entities);
        return source;
    }

    // Flattens a plan back into "uuid -> where it ended up", which is what every preservation
    // assertion below is really about.
    std::vector<std::pair<uint64_t, world::SectorCoord>> placements(const world::RepartitionPlan& plan)
    {
        std::vector<std::pair<uint64_t, world::SectorCoord>> result;
        for (const auto& target : plan.targets)
        {
            for (const auto& node : target.entities)
                result.emplace_back(node.value("uuid", uint64_t{0}), target.coord);
        }
        std::sort(result.begin(), result.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
        return result;
    }

    glm::vec3 positionOf(const json& node)
    {
        const auto& p = node.at("transform").at("position");
        return glm::vec3(p[0].get<float>(), p[1].get<float>(), p[2].get<float>());
    }

    bool hasTarget(const world::RepartitionPlan& plan, const world::SectorCoord& coord)
    {
        return std::any_of(plan.targets.begin(), plan.targets.end(),
                           [&](const world::RepartitionTargetSector& t) { return t.coord == coord; });
    }

    const world::RepartitionTargetSector* findTarget(const world::RepartitionPlan& plan,
                                                     const world::SectorCoord& coord)
    {
        for (const auto& target : plan.targets)
        {
            if (target.coord == coord)
                return &target;
        }
        return nullptr;
    }

    fs::path testRoot()
    {
        return fs::temp_directory_path() / "vf_sector_repartition_tests";
    }

    void resetTestRoot()
    {
        std::error_code ec;
        fs::remove_all(testRoot(), ec);
        fs::create_directories(testRoot(), ec);
    }
}

TEST_SUITE("SectorRepartition")
{
    // ---- parity with the registry-side trait reader -------------------------------------

    TEST_CASE("readEntityStreamingTraitsFromJson agrees with readEntityStreamingTraits")
    {
        auto check = [](scene::Entity& entity)
        {
            const world::EntityStreamingTraits live = world::readEntityStreamingTraits(entity);
            const json node = serialization::SceneSerialization::serializeEntity(entity);
            const world::EntityStreamingTraits cold = world::readEntityStreamingTraitsFromJson(node);

            CHECK(cold.managedBySeparateSystem == live.managedBySeparateSystem);
            CHECK(cold.spatiallyLoaded == live.spatiallyLoaded);
            CHECK(cold.hasTransform == live.hasTransform);
            CHECK(cold.position.x == doctest::Approx(live.position.x));
            CHECK(cold.position.y == doctest::Approx(live.position.y));
            CHECK(cold.position.z == doctest::Approx(live.position.z));

            scene::EntityRegistry::getRegistry().destroy(entity.getHandle());
        };

        SUBCASE("plain entity")
        {
            auto e = makeEntity("Plain");
            e.getComponent<components::TransformComponent>().position = {250.0f, 3.0f, 50.0f};
            check(e);
        }
        SUBCASE("pinned as not spatially loaded")
        {
            auto e = makeEntity("Pinned");
            e.addComponent<components::StreamingPolicyComponent>().spatiallyLoaded = false;
            check(e);
        }
        SUBCASE("terrain")      { auto e = makeEntity("T");  e.addComponent<components::TerrainComponent>();     check(e); }
        SUBCASE("terrain tile") { auto e = makeEntity("TT"); e.addComponent<components::TerrainTileComponent>(); check(e); }
        SUBCASE("ocean")        { auto e = makeEntity("O");  e.addComponent<components::OceanComponent>();       check(e); }
        SUBCASE("ibl")          { auto e = makeEntity("I");  e.addComponent<components::IBLComponent>();         check(e); }
        SUBCASE("camera")       { auto e = makeEntity("C");  e.addComponent<components::CameraComponent>();      check(e); }
    }

    TEST_CASE("a node with no transform reports hasTransform == false")
    {
        // serializeEntity omits "transform" entirely for an entity without the component, so
        // presence of the key IS hasTransform. Constructed by hand because scene::Entity always
        // carries a TransformComponent.
        json node;
        node["uuid"] = 1u;
        node["name"] = "NoTransform";
        node["components"] = json::object();

        const auto traits = world::readEntityStreamingTraitsFromJson(node);
        CHECK_FALSE(traits.hasTransform);
        CHECK(world::resolveSectorAssignment(traits, makeConfig(128.0f)).kind
              == world::SectorAssignmentKind::NoTransform);
    }

    // ---- the acceptance criterion: 128 -> 256 -> 128 -------------------------------------

    TEST_CASE("128 -> 256 -> 128 preserves entity count, UUIDs and transforms")
    {
        const auto small = makeConfig(128.0f);
        const auto large = makeConfig(256.0f);

        // Four sectors of the 128 grid collapse into one 256 sector, plus one entity well away
        // from the origin so the round trip has something to move back.
        std::vector<json> originals;
        originals.push_back(entityNode("A", 1598001, {  10.0f, 0.0f,   10.0f}));
        originals.push_back(entityNode("B", 1598002, { 200.0f, 5.0f,   30.0f}));
        originals.push_back(entityNode("C", 1598003, {  30.0f, 0.0f,  200.0f}));
        originals.push_back(entityNode("D", 1598004, { 260.0f, 0.0f,  260.0f}));
        originals.push_back(entityNode("E", 1598005, {-130.0f, 0.0f, -130.0f}));

        std::vector<world::RepartitionSourceSector> sources;
        for (auto& node : originals)
        {
            const auto coord = world::worldPositionToSectorCoord(positionOf(node), small);
            auto it = std::find_if(sources.begin(), sources.end(),
                                   [&](const world::RepartitionSourceSector& s) { return s.coord == coord; });
            if (it == sources.end())
            {
                sources.push_back(sourceSector(coord, {node}));
            }
            else
            {
                it->entities.push_back(node);
            }
        }

        world::RepartitionPlan up = world::planRepartition(sources, small, large);
        REQUIRE(up.summary.valid);
        CHECK(up.summary.entityCount == 5);
        CHECK(up.summary.duplicatesDropped == 0);

        // A, B and C all live inside 256-sector (0,0); D is in (1,1) and E in (-1,-1).
        CHECK(up.summary.targetSectorCount == 3);
        CHECK(hasTarget(up, world::SectorCoord(0, 0)));
        CHECK(hasTarget(up, world::SectorCoord(1, 1)));
        CHECK(hasTarget(up, world::SectorCoord(-1, -1)));

        // Feed the result straight back in as sources, at the original size.
        std::vector<world::RepartitionSourceSector> back;
        for (auto& target : up.targets)
            back.push_back(sourceSector(target.coord, std::move(target.entities)));

        world::RepartitionPlan down = world::planRepartition(back, large, small);
        REQUIRE(down.summary.valid);
        CHECK(down.summary.entityCount == 5);

        // Every original node, byte-identical, and back in the sector it started in.
        const auto finalPlacements = placements(down);
        REQUIRE(finalPlacements.size() == originals.size());

        for (const auto& original : originals)
        {
            const uint64_t uuid = original.at("uuid").get<uint64_t>();
            const world::SectorCoord expected =
                world::worldPositionToSectorCoord(positionOf(original), small);

            const auto* target = findTarget(down, expected);
            REQUIRE(target != nullptr);

            auto match = std::find_if(target->entities.begin(), target->entities.end(),
                                      [&](const json& n) { return n.value("uuid", uint64_t{0}) == uuid; });
            REQUIRE(match != target->entities.end());

            // The node was MOVED, never rebuilt - so this is equality of the whole subtree, not
            // just of the fields the migration happens to read.
            CHECK(*match == original);
        }
    }

    // ---- boundaries and negative coordinates --------------------------------------------

    TEST_CASE("an entity exactly on a sector boundary lands where the coord math puts it")
    {
        const auto config = makeConfig(100.0f);

        std::vector<world::RepartitionSourceSector> sources;
        sources.push_back(sourceSector({0, 0}, {
            entityNode("OnEdge",   1598010, {100.0f, 0.0f,   0.0f}),
            entityNode("JustUnder",1598011, { 99.999f, 0.0f, 0.0f}),
        }));

        world::RepartitionPlan plan = world::planRepartition(sources, makeConfig(200.0f), config);
        REQUIRE(plan.summary.valid);

        const auto placed = placements(plan);
        REQUIRE(placed.size() == 2);
        CHECK(placed[0].second == world::SectorCoord(1, 0)); // 1598010, the boundary
        CHECK(placed[1].second == world::SectorCoord(0, 0)); // 1598011
    }

    TEST_CASE("negative coordinates floor rather than truncate toward zero")
    {
        const auto config = makeConfig(100.0f);

        std::vector<world::RepartitionSourceSector> sources;
        sources.push_back(sourceSector({-1, -1}, {
            entityNode("JustBelowZero", 1598020, { -0.001f, 0.0f, -0.001f}),
            entityNode("NegBoundary",   1598021, {-100.0f,  0.0f, -100.0f}),
            entityNode("NegZero",       1598022, {  -0.0f,  0.0f,   -0.0f}),
        }));

        world::RepartitionPlan plan = world::planRepartition(sources, makeConfig(200.0f), config);
        const auto placed = placements(plan);
        REQUIRE(placed.size() == 3);
        CHECK(placed[0].second == world::SectorCoord(-1, -1));
        CHECK(placed[1].second == world::SectorCoord(-1, -1));
        CHECK(placed[2].second == world::SectorCoord(0, 0)); // -0.0f is sector 0, not -1
    }

    // ---- the duplicate-record heal -------------------------------------------------------

    TEST_CASE("an entity listed twice in a source file is written once")
    {
        // The fingerprint of the pre-VK-1598 save path: WorldSector::entityUUIDs held every UUID
        // twice after a load, and buildSectorJson iterates that list.
        const json node = entityNode("Doubled", 1598030, {10.0f, 0.0f, 10.0f});

        std::vector<world::RepartitionSourceSector> sources;
        sources.push_back(sourceSector({0, 0}, {node, node, node}));

        world::RepartitionPlan plan = world::planRepartition(sources, makeConfig(128.0f),
                                                             makeConfig(256.0f));
        REQUIRE(plan.summary.valid);
        CHECK(plan.summary.entityCount == 1);
        CHECK(plan.summary.duplicatesDropped == 2);
        REQUIRE(plan.targets.size() == 1);
        CHECK(plan.targets[0].entities.size() == 1);
    }

    // ---- nothing is ever dropped ---------------------------------------------------------

    TEST_CASE("payloads that are not re-bucketed are still carried across")
    {
        // A terrain/ocean/IBL/camera payload and a pinned entity are excluded from BUCKETING, but
        // the .vfsector is the only copy of their data - discarding them would be data loss.
        auto terrain = makeEntity("TerrainPayload");
        terrain.addOrReplaceComponent<components::UUIDComponent>(uint64_t{1598040});
        terrain.addComponent<components::TerrainComponent>();
        terrain.getComponent<components::TransformComponent>().position = {300.0f, 0.0f, 300.0f};

        auto pinned = makeEntity("PinnedPayload");
        pinned.addOrReplaceComponent<components::UUIDComponent>(uint64_t{1598041});
        pinned.addComponent<components::StreamingPolicyComponent>().spatiallyLoaded = false;
        pinned.getComponent<components::TransformComponent>().position = {300.0f, 0.0f, 300.0f};

        json noTransform;
        noTransform["uuid"] = 1598042u;
        noTransform["name"] = "NoTransformPayload";
        noTransform["components"] = json::object();

        std::vector<world::RepartitionSourceSector> sources;
        sources.push_back(sourceSector({2, 2}, {serializeThrowaway(terrain),
                                                serializeThrowaway(pinned),
                                                noTransform}));

        world::RepartitionPlan plan = world::planRepartition(sources, makeConfig(128.0f),
                                                             makeConfig(256.0f));
        REQUIRE(plan.summary.valid);
        CHECK(plan.summary.entityCount == 3);
        CHECK(plan.summary.carriedNonSpatial == 3);

        // The two with a transform bucket by position; the one without follows its source sector's
        // centre - (2,2) at 128 is centred on (320, 320), which is 256-sector (1,1).
        const auto placed = placements(plan);
        REQUIRE(placed.size() == 3);
        for (const auto& [uuid, coord] : placed)
            CHECK(coord == world::SectorCoord(1, 1));
    }

    // ---- data-layer fan-out --------------------------------------------------------------

    TEST_CASE("overlappingTargetSectors maps a footprint onto the new grid")
    {
        SUBCASE("growing: four sources share one target")
        {
            const auto small = makeConfig(128.0f);
            const auto large = makeConfig(256.0f);
            for (const world::SectorCoord& source : {world::SectorCoord(0, 0), world::SectorCoord(1, 0),
                                                     world::SectorCoord(0, 1), world::SectorCoord(1, 1)})
            {
                const auto overlaps = world::overlappingTargetSectors(source, small, large);
                REQUIRE(overlaps.size() == 1);
                CHECK(overlaps[0] == world::SectorCoord(0, 0));
            }
        }

        SUBCASE("shrinking: one source spans four targets")
        {
            const auto overlaps = world::overlappingTargetSectors({0, 0}, makeConfig(256.0f),
                                                                  makeConfig(128.0f));
            REQUIRE(overlaps.size() == 4);
            CHECK(std::find(overlaps.begin(), overlaps.end(), world::SectorCoord(0, 0)) != overlaps.end());
            CHECK(std::find(overlaps.begin(), overlaps.end(), world::SectorCoord(1, 0)) != overlaps.end());
            CHECK(std::find(overlaps.begin(), overlaps.end(), world::SectorCoord(0, 1)) != overlaps.end());
            CHECK(std::find(overlaps.begin(), overlaps.end(), world::SectorCoord(1, 1)) != overlaps.end());
        }

        SUBCASE("a shared edge does not report the neighbour beyond it")
        {
            // Source (1,0) at 128 covers [128, 256) x [0, 128). Its upper X edge sits exactly on
            // the 256-grid boundary, which belongs to the NEXT sector - so it must not appear.
            const auto overlaps = world::overlappingTargetSectors({1, 0}, makeConfig(128.0f),
                                                                  makeConfig(256.0f));
            REQUIRE(overlaps.size() == 1);
            CHECK(overlaps[0] == world::SectorCoord(0, 0));
        }

        SUBCASE("a non-integer ratio straddles")
        {
            // 128 -> 192: source (1,0) covers [128, 256), which crosses the 192 boundary.
            const auto overlaps = world::overlappingTargetSectors({1, 0}, makeConfig(128.0f),
                                                                  makeConfig(192.0f));
            REQUIRE(overlaps.size() == 2);
            CHECK(overlaps[0] == world::SectorCoord(0, 0));
            CHECK(overlaps[1] == world::SectorCoord(1, 0));
        }
    }

    TEST_CASE("a data-layer blob is copied to every overlapping target")
    {
        std::vector<world::RepartitionSourceSector> sources;
        auto source = sourceSector({0, 0}, {entityNode("WithLayer", 1598050, {10.0f, 0.0f, 10.0f})});
        source.dataLayers["fog"] = {1, 2, 3, 4};
        sources.push_back(std::move(source));

        // 256 -> 128 splits the one source across four targets. The blob is opaque, so it cannot be
        // split - every target gets the whole thing.
        world::RepartitionPlan plan = world::planRepartition(sources, makeConfig(256.0f),
                                                             makeConfig(128.0f));
        REQUIRE(plan.summary.valid);
        CHECK(plan.summary.layerCopies == 4);
        CHECK(plan.summary.layerNameCollisions == 0);
        CHECK(plan.summary.targetSectorCount == 4);

        for (const auto& target : plan.targets)
        {
            REQUIRE(target.dataLayers.count("fog") == 1);
            CHECK(target.dataLayers.at("fog") == std::vector<uint8_t>{1, 2, 3, 4});
        }
    }

    TEST_CASE("a data-layer name collision resolves to the first source in (z,x) order")
    {
        // Two 128 sectors merging into one 256 sector, both carrying a layer called "fog".
        std::vector<world::RepartitionSourceSector> sources;

        auto later = sourceSector({1, 0}, {});
        later.dataLayers["fog"] = {9, 9};
        sources.push_back(std::move(later));

        auto first = sourceSector({0, 0}, {});
        first.dataLayers["fog"] = {1, 1};
        sources.push_back(std::move(first));

        world::RepartitionPlan plan = world::planRepartition(sources, makeConfig(128.0f),
                                                             makeConfig(256.0f));
        REQUIRE(plan.summary.valid);
        CHECK(plan.summary.layerCopies == 1);
        CHECK(plan.summary.layerNameCollisions == 1);
        REQUIRE(plan.targets.size() == 1);

        // (0,0) sorts before (1,0), so it wins regardless of the order the sources arrived in.
        CHECK(plan.targets[0].dataLayers.at("fog") == std::vector<uint8_t>{1, 1});
    }

    // ---- refusals -------------------------------------------------------------------------

    TEST_CASE("isWithinAddressableExtent sees what the clamping coord math hides")
    {
        // worldPositionToSectorCoord CLAMPS, so its output is always in range and cannot be used to
        // detect the hazard. This is the check that can.
        const auto config = makeConfig(128.0f);
        const float justInside = static_cast<float>(world::kMaxSectorCoord) * 128.0f;
        const float wayOutside = 5'000'000.0f;

        CHECK(world::isWithinAddressableExtent({0.0f, 0.0f, 0.0f}, config));
        CHECK(world::isWithinAddressableExtent({justInside, 0.0f, 0.0f}, config));
        CHECK_FALSE(world::isWithinAddressableExtent({wayOutside, 0.0f, 0.0f}, config));
        CHECK_FALSE(world::isWithinAddressableExtent({0.0f, 0.0f, -wayOutside}, config));

        // The clamp is exactly why the naive check is useless.
        CHECK(world::isValidSectorCoord(
            world::worldPositionToSectorCoord({wayOutside, 0.0f, 0.0f}, config)));
    }

    TEST_CASE("a config that pushes an entity out of the addressable range is refused")
    {
        // 5,000,000 units out is sector 4882 at 1024 (fine) but 39062 at 128 - past
        // kMaxSectorCoord, where it would be clamped onto the boundary sector and share that
        // sector's streaming id with whatever legitimately lives there.
        std::vector<world::RepartitionSourceSector> sources;
        sources.push_back(sourceSector({4882, 0},
            {entityNode("FarOut", 1598060, {5'000'000.0f, 0.0f, 0.0f})}));

        world::RepartitionPlan plan = world::planRepartition(sources, makeConfig(1024.0f),
                                                             makeConfig(128.0f));
        CHECK_FALSE(plan.summary.valid);
        CHECK(plan.summary.outOfRangeEntities == 1);
        CHECK_FALSE(plan.summary.refusal.empty());
    }

    TEST_CASE("an entity that is ALREADY out of range does not block the migration")
    {
        // It is clamped today and would be clamped afterwards, so the repartition neither causes
        // nor worsens it - refusing would leave the user with no way to change sector size at all.
        std::vector<world::RepartitionSourceSector> sources;
        sources.push_back(sourceSector({world::kMaxSectorCoord, 0},
            {entityNode("AlreadyBroken", 1598061, {5'000'000.0f, 0.0f, 0.0f})}));

        world::RepartitionPlan plan = world::planRepartition(sources, makeConfig(64.0f),
                                                             makeConfig(128.0f));
        CHECK(plan.summary.outOfRangeEntities == 0);
        CHECK(plan.summary.valid);
    }

    TEST_CASE("terrain alignment is the rule the repartition guard applies")
    {
        // The guard in WorldSectorServiceImpl::repartitionRefusal delegates to this helper, so the
        // rule is pinned here rather than duplicated there.
        const float tileSize = 64.0f;
        CHECK(world::isSectorAlignedToTerrain(world::alignSectorConfigToTerrain(tileSize, 4), tileSize));
        CHECK(world::isSectorAlignedToTerrain(makeConfig(256.0f, 4), tileSize));
        CHECK_FALSE(world::isSectorAlignedToTerrain(makeConfig(200.0f, 4), tileSize));

        const auto aligned = world::alignSectorConfigToTerrain(tileSize, 8);
        CHECK(aligned.sectorWorldSize == doctest::Approx(512.0f));
        CHECK(aligned.alignedToTerrain);
    }

    TEST_CASE("an explosive data-layer fan-out is refused rather than partially copied")
    {
        std::vector<world::RepartitionSourceSector> sources;
        auto source = sourceSector({0, 0}, {});
        source.dataLayers["fog"] = {1};
        sources.push_back(std::move(source));

        // 100000 -> 1 is ~1e10 target cells per source sector. A fog-of-war grid copied into a
        // subset of the sectors that need it is worse than no migration at all.
        world::RepartitionPlan plan = world::planRepartition(sources, makeConfig(100000.0f),
                                                             makeConfig(1.0f));
        CHECK_FALSE(plan.summary.valid);
        CHECK(plan.summary.layerCopies == 0);
        CHECK(world::overlappingTargetSectors({0, 0}, makeConfig(100000.0f), makeConfig(1.0f)).empty());
    }

    TEST_CASE("both refusals are reported when a shrink triggers both")
    {
        // VK-1600 review: the out-of-range refusal assigned OVER summary.refusal, discarding the
        // data-layer fan-out message set earlier in the same pass. A drastic shrink trips both by
        // construction, so the user followed "use a larger sector size", did that, and walked
        // straight into a fan-out refusal they had never been shown. Neither ever produced a bad
        // plan - valid is false either way - but the diagnostic was actively misleading.
        std::vector<world::RepartitionSourceSector> sources;
        auto source = sourceSector({0, 0},
            {entityNode("FarOut", 1598090, {50'000.0f, 0.0f, 0.0f})});
        source.dataLayers["fog"] = {1};
        sources.push_back(std::move(source));

        // At 100000 the entity is sector 0 (in range); at 1 it is sector 50000, past
        // kMaxSectorCoord. The same shrink explodes the data-layer fan-out.
        world::RepartitionPlan plan = world::planRepartition(sources, makeConfig(100000.0f),
                                                             makeConfig(1.0f));

        CHECK_FALSE(plan.summary.valid);
        REQUIRE(plan.summary.outOfRangeEntities == 1);

        // Out-of-range still leads - it is the one the user can act on most directly - but the
        // fan-out message has to survive underneath it.
        CHECK(plan.summary.refusal.find("addressable sector range") != std::string::npos);
        CHECK(plan.summary.refusal.find("data-layer blob") != std::string::npos);
    }

    // ---- the JSON-driven writer ------------------------------------------------------------

    TEST_CASE("saveSectorFromEntityData round-trips through loadSector")
    {
        resetTestRoot();
        const std::string path = (testRoot() / "sector_3_-4.vfsector").string();

        std::vector<json> entities;
        entities.push_back(entityNode("W1", 1598070, {  10.0f, 1.0f,  20.0f}));
        entities.push_back(entityNode("W2", 1598071, {-100.0f, 2.0f, -50.0f}));

        world::SectorDataLayers layers;
        layers["fog"] = {7, 7, 7};

        REQUIRE(world::WorldSectorSerialization::saveSectorFromEntityData({3, -4}, entities, layers,
                                                                          path));

        std::vector<json> readBack;
        world::SectorDataLayers readLayers;
        REQUIRE(world::WorldSectorSerialization::loadSector(path, readBack, &readLayers));

        // Byte-identical nodes: the whole point of the cold path is that nothing is rebuilt.
        REQUIRE(readBack.size() == entities.size());
        for (size_t i = 0; i < entities.size(); ++i)
            CHECK(readBack[i] == entities[i]);

        REQUIRE(readLayers.count("fog") == 1);
        CHECK(readLayers.at("fog") == std::vector<uint8_t>{7, 7, 7});

        world::SectorMetadata metadata;
        REQUIRE(world::WorldSectorSerialization::readSectorMetadata(path, metadata));
        CHECK(metadata.valid);
        CHECK(metadata.entityCount == 2);
        // The AABB spans the two top-level positions, exactly as computeSectorAABB would.
        CHECK(metadata.bounds.min.x == doctest::Approx(-100.0f));
        CHECK(metadata.bounds.max.x == doctest::Approx(10.0f));

        std::error_code ec;
        fs::remove_all(testRoot(), ec);
    }

    TEST_CASE("a sector carrying only data layers still round-trips")
    {
        resetTestRoot();
        const std::string path = (testRoot() / "sector_0_0.vfsector").string();

        // The fan-out creates targets that received a blob but no entity. An empty entity array is
        // a legitimate sector, not a corrupt one.
        world::SectorDataLayers layers;
        layers["grid"] = {1, 2};

        REQUIRE(world::WorldSectorSerialization::saveSectorFromEntityData({0, 0}, {}, layers, path));

        std::vector<json> readBack;
        world::SectorDataLayers readLayers;
        REQUIRE(world::WorldSectorSerialization::loadSector(path, readBack, &readLayers));
        CHECK(readBack.empty());
        REQUIRE(readLayers.count("grid") == 1);
        CHECK(readLayers.at("grid") == std::vector<uint8_t>{1, 2});

        std::error_code ec;
        fs::remove_all(testRoot(), ec);
    }

    // ---- the entityUUIDs double-push fix ---------------------------------------------------

    TEST_CASE("a sector load leaves each UUID in entityUUIDs exactly once")
    {
        // Reproduces the WorldSectorServiceImpl::setOnEntityLoaded contract: finalizeSectorLoad
        // pre-populates entityUUIDs from the file's JSON, then the loader's callback assigns each
        // spawned entity. Without the remove-before-assign the list ends up holding every UUID
        // twice - and buildSectorJson iterates it, so the next Save World would write every entity
        // twice into the .vfsector, compounding on each cycle.
        scene::SceneGraphSystem sceneGraph;
        world::SectorEntityLoader loader;
        world::WorldSectorManager manager(makeConfig(128.0f));
        const world::SectorCoord coord{0, 0};

        const std::vector<uint64_t> uuids{1598080, 1598081, 1598082};
        std::vector<std::pair<std::string, json>> payload;
        for (size_t i = 0; i < uuids.size(); ++i)
        {
            payload.emplace_back("Loaded" + std::to_string(i),
                                 entityNode("Loaded" + std::to_string(i), uuids[i],
                                            glm::vec3(10.0f * static_cast<float>(i), 0.0f, 10.0f)));
        }

        // finalizeSectorLoad's pre-population.
        auto& sector = manager.getOrCreateSector(coord);
        for (uint64_t uuid : uuids)
            sector.entityUUIDs.push_back(uuid);

        loader.setOnEntityLoaded([&](uint64_t uuid, uint8_t, const world::SectorCoord& c)
        {
            glm::vec3 position(0.0f);
            auto entity = scene::EntityRegistry::findByUUID(uuid);
            if (entity != entt::null)
                position = scene::Entity(entity).getComponent<components::TransformComponent>().position;

            manager.removeEntityFromSector(uuid, c);
            manager.assignEntityToSector(uuid, position);
        });

        loader.queueSectorLoadFromData(world::kPrimaryGridIndex, coord, payload);
        loader.flush(sceneGraph);

        const auto* loaded = manager.getSector(coord);
        REQUIRE(loaded != nullptr);
        CHECK(loaded->entityUUIDs.size() == uuids.size());

        std::unordered_set<uint64_t> unique(loaded->entityUUIDs.begin(), loaded->entityUUIDs.end());
        CHECK(unique.size() == uuids.size());

        // Clean up through the loader's own destroy path.
        loader.queueSectorUnload(world::kPrimaryGridIndex, coord, uuids);
        loader.flush(sceneGraph);
    }
}
