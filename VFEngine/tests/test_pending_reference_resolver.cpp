#include <doctest.h>
#include <world/PendingReferenceResolver.hpp>
#include <world/SectorRefFieldRegistry.hpp>
#include <world/SectorEntityLoader.hpp>
#include <scene/SceneGraphSystem.hpp>
#include <scene/Entity.hpp>
#include <scene/EntityRegistry.hpp>
#include <serialization/SceneSerialization.hpp>
#include <components/Components.hpp>
#include <nlohmann/json.hpp>

#include <limits>
#include <string>
#include <utility>
#include <vector>

// ============================================================
// VK-1590 — cross-sector entity reference resolution.
//
// Three layers:
//   1. PendingReferenceResolver  — the pure ledger + drain queue (no registry needed).
//   2. SectorRefFieldRegistry    — the carrier table: read a target UUID out of a component,
//                                  write the resolved handle back in.
//   3. Streaming end-to-end      — the acceptance criteria, driven through SectorEntityLoader.
//
// Layer 3 hand-wires the loader and the resolver because WorldSectorServiceImpl is not
// unit-constructible (it takes a shared_ptr<SceneGraphSystem> and drives EventDispatcher
// throughout). It calls the PRODUCTION helpers — registerEntityReferences /
// applyResolvedReferences — in the production order, so only the few lines of service glue in
// WorldSectorServiceImpl.cpp and WorldSectorStreamingOps.cpp are left to the manual AC.
//
// UUIDs live in 915000-915999: scene::EntityRegistry is a process-global singleton shared by
// every test TU, so ranges must not collide (910xxx belongs to test_sector_entity_loader.cpp).
// ============================================================

namespace
{
    using json = nlohmann::json;

    // ---- layer 2/3 shared scaffolding -------------------------------------------------

    // Live entity that cleans itself out of the global registry when the test ends.
    class ScopedEntity
    {
    public:
        ScopedEntity(const std::string& name, uint64_t uuid)
        {
            scene::EntityRegistry::init(); // connect UUID lookup hooks (idempotent)
            scene::Entity entity(name);
            entity.addOrReplaceComponent<components::UUIDComponent>(uuid);
            handle = entity.getHandle();
        }

        ~ScopedEntity() { destroy(); }

        ScopedEntity(const ScopedEntity&) = delete;
        ScopedEntity& operator=(const ScopedEntity&) = delete;

        void destroy()
        {
            auto& registry = scene::EntityRegistry::getRegistry();
            if (handle != entt::null && registry.valid(handle))
            {
                registry.destroy(handle);
            }
            handle = entt::null;
        }

        [[nodiscard]] entt::entity get() const { return handle; }

    private:
        entt::entity handle{entt::null};
    };

    components::SocketAttachmentComponent& attachSocket(entt::entity entity,
                                                        uint64_t parentUUID,
                                                        const std::string& parentName = {})
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto& attachment = registry.emplace_or_replace<components::SocketAttachmentComponent>(entity);
        attachment.parentEntityUUID = parentUUID;
        attachment.parentEntityName = parentName;
        attachment.socketName = "hand_r";
        attachment.needsParentResolution = true;
        return attachment;
    }

    // Serialize a throwaway entity into the JSON shape sector files store, then destroy it so
    // the loader is the one bringing it back. Mirrors test_sector_entity_loader.cpp's helper.
    std::pair<std::string, json> makeEntityPayload(const std::string& name, uint64_t uuid,
                                                   uint64_t socketParentUUID = 0,
                                                   const std::string& socketParentName = {})
    {
        scene::EntityRegistry::init();
        scene::Entity entity(name);
        entity.addOrReplaceComponent<components::UUIDComponent>(uuid);

        if (socketParentUUID != 0 || !socketParentName.empty())
        {
            attachSocket(entity.getHandle(), socketParentUUID, socketParentName);
        }

        json entityJson = serialization::SceneSerialization::serializeEntity(entity);
        scene::EntityRegistry::getRegistry().destroy(entity.getHandle());
        return {name, entityJson};
    }

    // The socket block as it lands in a .vfsector entity payload, or null if absent.
    // Goes through the public serializeEntity dispatch — serializeSocketAttachment itself is a
    // private member of SceneSerialization.
    const json* socketJsonOf(const std::pair<std::string, json>& payload)
    {
        const json& entityJson = payload.second;
        if (!entityJson.contains("components"))
            return nullptr;
        const json& components = entityJson["components"];
        if (!components.contains("socketAttachment"))
            return nullptr;
        return &components["socketAttachment"];
    }

    const components::SocketAttachmentComponent* socketOf(uint64_t uuid)
    {
        auto entity = scene::EntityRegistry::findByUUID(uuid);
        if (entity == entt::null)
            return nullptr;
        return scene::EntityRegistry::getRegistry().try_get<components::SocketAttachmentComponent>(entity);
    }

    // doctest's expression decomposer wraps the LHS in Expression_lhs<>, which then matches
    // entt's templated operator==(const Entity, entt::null_t) as well as doctest's own — an
    // ambiguity. Collapse entity/null comparisons to a plain bool before they reach CHECK.
    bool entityExists(uint64_t uuid)
    {
        return scene::EntityRegistry::findByUUID(uuid) != entt::null;
    }

    bool isUnbound(const components::SocketAttachmentComponent* attachment)
    {
        return attachment != nullptr && attachment->parentEntity == entt::null;
    }

    // Mirrors the file-static helper in WorldSectorStreamingOps.cpp: the resolver must be fed
    // what is actually resident, expanded through nested children.
    void collectSubtreeUUIDs(entt::registry& registry, entt::entity entity,
                             std::vector<uint64_t>& out)
    {
        if (entity == entt::null || !registry.valid(entity))
            return;
        if (const auto* uuidComp = registry.try_get<components::UUIDComponent>(entity))
            out.push_back(uuidComp->id.getValue());
        if (const auto* childrenComp = registry.try_get<components::ChildrenComponent>(entity))
        {
            for (auto child : childrenComp->children)
                collectSubtreeUUIDs(registry, child, out);
        }
    }

    std::vector<uint64_t> expandLiveUUIDs(const std::vector<uint64_t>& rootUUIDs)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        std::vector<uint64_t> live;
        for (uint64_t uuid : rootUUIDs)
            collectSubtreeUUIDs(registry, scene::EntityRegistry::findByUUID(uuid), live);
        return live;
    }

    // Drives SectorEntityLoader + PendingReferenceResolver through the exact sequence
    // WorldSectorServiceImpl::update() runs each frame.
    class StreamHarness
    {
    public:
        StreamHarness()
        {
            scene::EntityRegistry::init();
            loader.setOnEntityPostLoad([this](uint64_t uuid, const std::string&, const std::string&)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                std::vector<uint64_t> subtree;
                collectSubtreeUUIDs(registry, scene::EntityRegistry::findByUUID(uuid), subtree);
                for (uint64_t entityUUID : subtree)
                {
                    auto entity = scene::EntityRegistry::findByUUID(entityUUID);
                    world::SectorRefFieldRegistry::registerEntityReferences(
                        registry, entity, entityUUID, resolver, probeQueue);
                }
            });
        }

        ~StreamHarness()
        {
            // Tear down through the loader's own unload path, so the scene graph does not keep
            // dangling child handles in the (process-global) registry.
            for (const auto& sector : sectors)
            {
                std::vector<uint64_t> alive;
                for (uint64_t uuid : sector.uuids)
                {
                    if (scene::EntityRegistry::findByUUID(uuid) != entt::null)
                        alive.push_back(uuid);
                }
                loader.queueSectorUnload(sector.coord, alive);
            }
            loader.flush(sceneGraph);
        }

        void queueSector(const world::SectorCoord& coord,
                         std::vector<std::pair<std::string, json>> payloads,
                         std::vector<uint64_t> rootUUIDs)
        {
            loader.queueSectorLoadFromData(coord, payloads);
            sectors.push_back({coord, std::move(rootUUIDs), true});
        }

        // One "frame": drain spawns, probe already-resident targets, promote completed sectors,
        // then apply. Same order as WorldSectorServiceImpl::update().
        void tick(int maxEntitiesPerFrame = std::numeric_limits<int>::max())
        {
            auto& registry = scene::EntityRegistry::getRegistry();

            loader.update(sceneGraph, maxEntitiesPerFrame);

            if (!probeQueue.empty())
            {
                resolver.onSectorLoaded(probeQueue);
                probeQueue.clear();
            }

            for (auto& sector : sectors)
            {
                if (sector.loading && !loader.hasPendingLoadsForSector(sector.coord))
                {
                    sector.loading = false;
                    resolver.onSectorLoaded(expandLiveUUIDs(sector.uuids));
                }
            }

            world::SectorRefFieldRegistry::applyResolvedReferences(registry, resolver);
        }

        // Runs frames until every queued sector has finished spawning.
        void pumpUntilIdle(int maxEntitiesPerFrame = std::numeric_limits<int>::max())
        {
            for (int guard = 0; guard < 256; ++guard)
            {
                tick(maxEntitiesPerFrame);
                if (loader.pendingLoadCount() == 0)
                    return;
            }
            FAIL("sector loads did not drain");
        }

        void loadSector(const world::SectorCoord& coord,
                        std::vector<std::pair<std::string, json>> payloads,
                        std::vector<uint64_t> rootUUIDs,
                        int maxEntitiesPerFrame = std::numeric_limits<int>::max())
        {
            queueSector(coord, std::move(payloads), std::move(rootUUIDs));
            pumpUntilIdle(maxEntitiesPerFrame);
        }

        // Mirrors WorldSectorServiceImpl::handleSectorUnload for static entities.
        void unloadSector(const world::SectorCoord& coord)
        {
            for (auto& sector : sectors)
            {
                if (!(sector.coord == coord))
                    continue;

                const std::vector<uint64_t> subtree = expandLiveUUIDs(sector.uuids);
                resolver.onSectorUnloaded(subtree);
                resolver.removeReferencesFrom(subtree);
                loader.queueSectorUnload(coord, sector.uuids); // ROOT list, as in production
                loader.flush(sceneGraph);
                sector.loading = false;
            }
        }

        void forgetSector(const world::SectorCoord& coord)
        {
            std::erase_if(sectors, [&](const SectorEntry& s) { return s.coord == coord; });
        }

        world::PendingReferenceResolver resolver;
        world::SectorEntityLoader loader;
        scene::SceneGraphSystem sceneGraph;
        std::vector<uint64_t> probeQueue;

    private:
        struct SectorEntry
        {
            world::SectorCoord coord;
            std::vector<uint64_t> uuids;
            bool loading = false;
        };

        std::vector<SectorEntry> sectors;
    };
}

// ============================================================
// 1. PendingReferenceResolver — pure ledger, no registry
// ============================================================

TEST_SUITE("PendingReferenceResolver")
{
    TEST_CASE("reference resolves when its target sector loads")
    {
        world::PendingReferenceResolver resolver;
        resolver.addPendingReference(100, 200, world::ReferenceType::Parent);
        CHECK(resolver.pendingCount() == 1);
        CHECK(resolver.resolvedCount() == 0);

        resolver.onSectorLoaded({200});
        CHECK(resolver.pendingCount() == 0);
        REQUIRE(resolver.resolvedCount() == 1);

        const auto& resolved = resolver.getResolved();
        CHECK(resolved[0].sourceUUID == 100);
        CHECK(resolved[0].targetUUID == 200);
        CHECK(resolved[0].type == world::ReferenceType::Parent);
    }

    TEST_CASE("unrelated sector loads do not resolve references")
    {
        world::PendingReferenceResolver resolver;
        resolver.addPendingReference(100, 200, world::ReferenceType::SocketAttachment);

        resolver.onSectorLoaded({300, 400});
        CHECK(resolver.pendingCount() == 1);
        CHECK(resolver.resolvedCount() == 0);
    }

    TEST_CASE("resolved reference re-pends when its target unloads")
    {
        world::PendingReferenceResolver resolver;
        resolver.addPendingReference(100, 200, world::ReferenceType::IKTarget);
        resolver.onSectorLoaded({200});
        REQUIRE(resolver.resolvedCount() == 1);

        resolver.onSectorUnloaded({200});
        CHECK(resolver.resolvedCount() == 0);
        CHECK(resolver.pendingCount() == 1);

        // Survives repeated load/unload cycles
        resolver.onSectorLoaded({200});
        CHECK(resolver.resolvedCount() == 1);
        resolver.onSectorUnloaded({200});
        CHECK(resolver.pendingCount() == 1);
    }

    TEST_CASE("clearResolved drops consumed references without touching pending")
    {
        world::PendingReferenceResolver resolver;
        resolver.addPendingReference(1, 2, world::ReferenceType::Parent);
        resolver.addPendingReference(3, 4, world::ReferenceType::Parent);
        resolver.onSectorLoaded({2});
        REQUIRE(resolver.resolvedCount() == 1);

        resolver.clearResolved();
        CHECK(resolver.resolvedCount() == 0);
        CHECK(resolver.pendingCount() == 1);
    }

    TEST_CASE("addPendingReference ignores a duplicate triple")
    {
        world::PendingReferenceResolver resolver;
        resolver.addPendingReference(1, 2, world::ReferenceType::SocketAttachment);
        resolver.addPendingReference(1, 2, world::ReferenceType::SocketAttachment);
        CHECK(resolver.pendingCount() == 1);

        // A different type is a different reference.
        resolver.addPendingReference(1, 2, world::ReferenceType::Parent);
        CHECK(resolver.pendingCount() == 2);

        // Dedup also consults the resolved ledger, so a source that respawns while its
        // reference is already bound does not double-register.
        resolver.onSectorLoaded({2});
        REQUIRE(resolver.resolvedCount() == 2);
        resolver.addPendingReference(1, 2, world::ReferenceType::SocketAttachment);
        CHECK(resolver.pendingCount() == 0);
        CHECK(resolver.resolvedCount() == 2);
    }

    TEST_CASE("consumeNewlyResolved drains once and leaves the ledger intact")
    {
        world::PendingReferenceResolver resolver;
        resolver.addPendingReference(1, 2, world::ReferenceType::SocketAttachment);
        CHECK(resolver.newlyResolvedCount() == 0);

        resolver.onSectorLoaded({2});
        CHECK(resolver.newlyResolvedCount() == 1);

        const auto batch = resolver.consumeNewlyResolved();
        REQUIRE(batch.size() == 1);
        CHECK(batch[0].sourceUUID == 1);
        CHECK(batch[0].targetUUID == 2);

        CHECK(resolver.consumeNewlyResolved().empty()); // one-shot
        CHECK(resolver.resolvedCount() == 1);           // ledger untouched
    }

    TEST_CASE("removeReferencesFrom drops entries from both queues")
    {
        world::PendingReferenceResolver resolver;
        resolver.addPendingReference(1, 2, world::ReferenceType::SocketAttachment); // -> resolved
        resolver.addPendingReference(1, 3, world::ReferenceType::SocketAttachment); // stays pending
        resolver.addPendingReference(9, 3, world::ReferenceType::SocketAttachment); // other source
        resolver.onSectorLoaded({2});
        REQUIRE(resolver.resolvedCount() == 1);
        REQUIRE(resolver.pendingCount() == 2);

        resolver.removeReferencesFrom({1});
        CHECK(resolver.resolvedCount() == 0);
        CHECK(resolver.pendingCount() == 1); // 9 -> 3 survives

        resolver.removeReferencesFrom({}); // empty list is a no-op
        CHECK(resolver.pendingCount() == 1);
    }

    TEST_CASE("clear empties every queue")
    {
        world::PendingReferenceResolver resolver;
        resolver.addPendingReference(1, 2, world::ReferenceType::SocketAttachment);
        resolver.addPendingReference(3, 4, world::ReferenceType::SocketAttachment);
        resolver.onSectorLoaded({2});

        resolver.clear();
        CHECK(resolver.pendingCount() == 0);
        CHECK(resolver.resolvedCount() == 0);
        CHECK(resolver.newlyResolvedCount() == 0);
    }

    TEST_CASE("a resolved reference re-enters newlyResolved after unload and reload")
    {
        world::PendingReferenceResolver resolver;
        resolver.addPendingReference(1, 2, world::ReferenceType::SocketAttachment);
        resolver.onSectorLoaded({2});
        REQUIRE(resolver.consumeNewlyResolved().size() == 1);

        resolver.onSectorUnloaded({2});
        REQUIRE(resolver.pendingCount() == 1);
        CHECK(resolver.newlyResolvedCount() == 0);

        resolver.onSectorLoaded({2});
        CHECK(resolver.newlyResolvedCount() == 1); // re-applied on reload
    }
}

// ============================================================
// 2. SectorRefFieldRegistry — the carrier table
// ============================================================

TEST_SUITE("SectorRefFieldRegistry")
{
    TEST_CASE("collectReferences emits the socket attachment parent uuid")
    {
        ScopedEntity source("RefSource", 915001);
        attachSocket(source.get(), 915002);

        std::vector<world::PendingReference> refs;
        world::SectorRefFieldRegistry::collectReferences(
            scene::EntityRegistry::getRegistry(), source.get(), 915001, refs);

        REQUIRE(refs.size() == 1);
        CHECK(refs[0].sourceUUID == 915001);
        CHECK(refs[0].targetUUID == 915002);
        CHECK(refs[0].type == world::ReferenceType::SocketAttachment);
    }

    TEST_CASE("collectReferences ignores a name-only attachment")
    {
        // The prefab shape: instantiation re-mints UUIDs, so prefab-internal attachments carry
        // only a name and must never enter the UUID-keyed pending path.
        ScopedEntity source("PrefabPart", 915010);
        attachSocket(source.get(), /*parentUUID*/ 0, "RigRoot");

        std::vector<world::PendingReference> refs;
        world::SectorRefFieldRegistry::collectReferences(
            scene::EntityRegistry::getRegistry(), source.get(), 915010, refs);
        CHECK(refs.empty());
    }

    TEST_CASE("collectReferences skips a self-reference and an entity with no carrier")
    {
        ScopedEntity plain("NoCarrier", 915020);
        ScopedEntity selfRef("SelfRef", 915021);
        attachSocket(selfRef.get(), 915021);

        auto& registry = scene::EntityRegistry::getRegistry();
        std::vector<world::PendingReference> refs;
        world::SectorRefFieldRegistry::collectReferences(registry, plain.get(), 915020, refs);
        world::SectorRefFieldRegistry::collectReferences(registry, selfRef.get(), 915021, refs);
        CHECK(refs.empty());
    }

    TEST_CASE("applyReference writes the handle and resets the resolution caches")
    {
        ScopedEntity source("Turret", 915030);
        ScopedEntity target("Bunker", 915031);
        auto& attachment = attachSocket(source.get(), 915031);
        attachment.cachedSocketIndex = 7;
        attachment.parentKind = components::SocketAttachmentComponent::ParentKind::Static;

        const bool applied = world::SectorRefFieldRegistry::applyReference(
            scene::EntityRegistry::getRegistry(),
            {915030, 915031, world::ReferenceType::SocketAttachment});

        REQUIRE(applied);
        const auto* result = socketOf(915030);
        REQUIRE(result != nullptr);
        CHECK(result->parentEntity == target.get());
        CHECK(result->cachedSocketIndex == -1);
        CHECK(result->parentKind == components::SocketAttachmentComponent::ParentKind::Unknown);
        CHECK_FALSE(result->needsParentResolution);
    }

    TEST_CASE("applyReference fills an empty parent name but never overwrites one")
    {
        auto& registry = scene::EntityRegistry::getRegistry();

        SUBCASE("empty name is filled from the target")
        {
            ScopedEntity source("Turret", 915040);
            ScopedEntity target("Bunker", 915041);
            attachSocket(source.get(), 915041);

            REQUIRE(world::SectorRefFieldRegistry::applyReference(
                registry, {915040, 915041, world::ReferenceType::SocketAttachment}));
            CHECK(socketOf(915040)->parentEntityName == "Bunker");
        }

        SUBCASE("an authored name is preserved")
        {
            ScopedEntity source("Turret", 915042);
            ScopedEntity target("Bunker", 915043);
            attachSocket(source.get(), 915043, "AuthoredName");

            REQUIRE(world::SectorRefFieldRegistry::applyReference(
                registry, {915042, 915043, world::ReferenceType::SocketAttachment}));
            CHECK(socketOf(915042)->parentEntityName == "AuthoredName");
        }
    }

    TEST_CASE("applyReference returns false when an endpoint is missing")
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        ScopedEntity target("Bunker", 915051);

        // Source never existed.
        CHECK_FALSE(world::SectorRefFieldRegistry::applyReference(
            registry, {915050, 915051, world::ReferenceType::SocketAttachment}));

        // Source exists but the target does not.
        ScopedEntity source("Turret", 915052);
        attachSocket(source.get(), 915053);
        CHECK_FALSE(world::SectorRefFieldRegistry::applyReference(
            registry, {915052, 915053, world::ReferenceType::SocketAttachment}));

        // Source exists but carries no attachment component.
        ScopedEntity bare("Bare", 915054);
        CHECK_FALSE(world::SectorRefFieldRegistry::applyReference(
            registry, {915054, 915051, world::ReferenceType::SocketAttachment}));
    }

    TEST_CASE("reserved reference types have no table row")
    {
        // Parent and IKTarget are documented exclusions: scene hierarchy is JSON nesting, and
        // IK chains are bone-name scoped. Neither is resolvable, so neither may silently no-op
        // into "applied".
        ScopedEntity source("Turret", 915060);
        ScopedEntity target("Bunker", 915061);
        attachSocket(source.get(), 915061);

        auto& registry = scene::EntityRegistry::getRegistry();
        CHECK_FALSE(world::SectorRefFieldRegistry::applyReference(
            registry, {915060, 915061, world::ReferenceType::Parent}));
        CHECK_FALSE(world::SectorRefFieldRegistry::applyReference(
            registry, {915060, 915061, world::ReferenceType::IKTarget}));

        CHECK(world::SectorRefFieldRegistry::fieldCount() == 1);
    }

    TEST_CASE("collectAllReferences finds every carrier in the registry")
    {
        ScopedEntity a("A", 915070);
        ScopedEntity b("B", 915071);
        ScopedEntity c("C", 915072);
        attachSocket(a.get(), 915072);
        attachSocket(b.get(), 915072);

        std::vector<world::PendingReference> refs;
        world::SectorRefFieldRegistry::collectAllReferences(
            scene::EntityRegistry::getRegistry(), refs);

        size_t mine = 0;
        for (const auto& ref : refs)
        {
            if (ref.targetUUID == 915072)
            {
                ++mine;
                CHECK((ref.sourceUUID == 915070 || ref.sourceUUID == 915071));
            }
        }
        CHECK(mine == 2);
    }

    TEST_CASE("parentEntityUUID survives the sector entity payload round trip")
    {
        SUBCASE("a name-only attachment writes no uuid key")
        {
            // The prefab / pre-VK-1590 shape. The key must stay absent so those payloads keep
            // serializing byte-identically.
            const auto payload = makeEntityPayload("PrefabPart", 915080, 0, "RigRoot");
            const json* socketJson = socketJsonOf(payload);
            REQUIRE(socketJson != nullptr);
            CHECK((*socketJson)["parentEntityName"].get<std::string>() == "RigRoot");
            CHECK_FALSE(socketJson->contains("parentEntityUUID"));
        }

        SUBCASE("a bound attachment writes the uuid key")
        {
            const auto payload = makeEntityPayload("Turret", 915082, 915083);
            const json* socketJson = socketJsonOf(payload);
            REQUIRE(socketJson != nullptr);
            REQUIRE(socketJson->contains("parentEntityUUID"));
            CHECK((*socketJson)["parentEntityUUID"].get<uint64_t>() == 915083);
        }

        SUBCASE("the loaded component arms on the uuid with the handle still null")
        {
            StreamHarness harness;
            // Target 915083 is never loaded, so nothing resolves the handle.
            harness.loadSector({915, 8}, {makeEntityPayload("Turret", 915082, 915083)}, {915082});

            const auto* attachment = socketOf(915082);
            REQUIRE(attachment != nullptr);
            CHECK(attachment->parentEntityUUID == 915083);
            CHECK(attachment->needsParentResolution); // armed even with no parentEntityName
            CHECK(isUnbound(attachment));
        }
    }
}

// ============================================================
// 3. Streaming end-to-end — the VK-1590 acceptance criteria
// ============================================================

TEST_SUITE("SectorReferenceStreaming")
{
    // AC 1
    TEST_CASE("cross-sector reference to an unloaded sector stays null")
    {
        const world::SectorCoord sectorA{915, 0};
        StreamHarness harness;

        harness.loadSector(sectorA, {makeEntityPayload("Turret", 915100, 915101)}, {915100});

        const auto* attachment = socketOf(915100);
        REQUIRE(attachment != nullptr);
        CHECK(isUnbound(attachment));
        CHECK(harness.resolver.pendingCount() == 1);
        CHECK(harness.resolver.resolvedCount() == 0);
    }

    // AC 2
    TEST_CASE("loading the target sector binds the reference")
    {
        const world::SectorCoord sectorA{915, 1};
        const world::SectorCoord sectorB{916, 1};
        StreamHarness harness;

        harness.loadSector(sectorA, {makeEntityPayload("Turret", 915110, 915111)}, {915110});
        REQUIRE(isUnbound(socketOf(915110)));

        harness.loadSector(sectorB, {makeEntityPayload("Bunker", 915111)}, {915111});

        CHECK(socketOf(915110)->parentEntity == scene::EntityRegistry::findByUUID(915111));
        CHECK(harness.resolver.pendingCount() == 0);
        CHECK(harness.resolver.resolvedCount() == 1);
        // The empty authored name was back-filled from the resolved target.
        CHECK(socketOf(915110)->parentEntityName == "Bunker");
    }

    // AC 3 — the "arbitrary approach direction" case, in unit form.
    TEST_CASE("permuted load orders converge")
    {
        const world::SectorCoord sectorA{915, 2};
        const world::SectorCoord sectorB{916, 2};

        SUBCASE("source sector first")
        {
            StreamHarness harness;
            harness.loadSector(sectorA, {makeEntityPayload("Turret", 915120, 915121)}, {915120});
            harness.loadSector(sectorB, {makeEntityPayload("Bunker", 915121)}, {915121});

            CHECK(socketOf(915120)->parentEntity == scene::EntityRegistry::findByUUID(915121));
            CHECK(harness.resolver.pendingCount() == 0);
            CHECK(harness.resolver.resolvedCount() == 1);
        }

        SUBCASE("target sector first")
        {
            StreamHarness harness;
            harness.loadSector(sectorB, {makeEntityPayload("Bunker", 915121)}, {915121});
            harness.loadSector(sectorA, {makeEntityPayload("Turret", 915120, 915121)}, {915120});

            // Identical terminal state, reached through the already-resident probe instead.
            CHECK(socketOf(915120)->parentEntity == scene::EntityRegistry::findByUUID(915121));
            CHECK(harness.resolver.pendingCount() == 0);
            CHECK(harness.resolver.resolvedCount() == 1);
        }
    }

    // AC 4
    TEST_CASE("reload after unload re-registers and re-resolves")
    {
        SUBCASE("the target sector cycles")
        {
            const world::SectorCoord sectorA{915, 3};
            const world::SectorCoord sectorB{916, 3};
            StreamHarness harness;

            harness.loadSector(sectorA, {makeEntityPayload("Turret", 915130, 915131)}, {915130});
            harness.loadSector(sectorB, {makeEntityPayload("Bunker", 915131)}, {915131});
            REQUIRE(harness.resolver.resolvedCount() == 1);

            harness.unloadSector(sectorB);
            CHECK_FALSE(entityExists(915131));
            CHECK(harness.resolver.resolvedCount() == 0);
            CHECK(harness.resolver.pendingCount() == 1); // demoted, source still alive

            harness.forgetSector(sectorB);
            harness.loadSector(sectorB, {makeEntityPayload("Bunker", 915131)}, {915131});
            CHECK(socketOf(915130)->parentEntity == scene::EntityRegistry::findByUUID(915131));
            CHECK(harness.resolver.resolvedCount() == 1);
        }

        SUBCASE("the source sector cycles")
        {
            const world::SectorCoord sectorA{915, 4};
            const world::SectorCoord sectorB{916, 4};
            StreamHarness harness;

            harness.loadSector(sectorA, {makeEntityPayload("Turret", 915140, 915141)}, {915140});
            harness.loadSector(sectorB, {makeEntityPayload("Bunker", 915141)}, {915141});
            REQUIRE(harness.resolver.resolvedCount() == 1);

            harness.unloadSector(sectorA);
            // The source is gone, so its ledger entry is dropped outright rather than left to
            // leak — otherwise dedup would suppress re-registration on respawn.
            CHECK(harness.resolver.resolvedCount() == 0);
            CHECK(harness.resolver.pendingCount() == 0);

            harness.forgetSector(sectorA);
            harness.loadSector(sectorA, {makeEntityPayload("Turret", 915140, 915141)}, {915140});
            CHECK(socketOf(915140)->parentEntity == scene::EntityRegistry::findByUUID(915141));
            CHECK(harness.resolver.resolvedCount() == 1);
        }
    }

    TEST_CASE("a source spawning before its target in the same sector still binds")
    {
        const world::SectorCoord sector{915, 5};
        StreamHarness harness;

        // maxEntitiesPerFrame = 1 forces the source to spawn a frame before the target, so the
        // registration lands while the target is still absent.
        harness.loadSector(sector,
                           {makeEntityPayload("Turret", 915150, 915151),
                            makeEntityPayload("Bunker", 915151)},
                           {915150, 915151},
                           /*maxEntitiesPerFrame*/ 1);

        CHECK(socketOf(915150)->parentEntity == scene::EntityRegistry::findByUUID(915151));
        CHECK(harness.resolver.pendingCount() == 0);
        CHECK(harness.resolver.resolvedCount() == 1);
    }

    TEST_CASE("a reference into a never-loaded sector stays pending without leaking")
    {
        const world::SectorCoord sectorA{915, 6};
        StreamHarness harness;

        harness.loadSector(sectorA, {makeEntityPayload("Turret", 915160, 915161)}, {915160});
        CHECK(harness.resolver.pendingCount() == 1);

        // Re-ticking must not re-register the same reference over and over.
        harness.tick();
        harness.tick();
        CHECK(harness.resolver.pendingCount() == 1);
        CHECK(harness.resolver.resolvedCount() == 0);
    }
}
