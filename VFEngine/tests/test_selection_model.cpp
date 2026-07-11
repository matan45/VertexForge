#include <doctest.h>
#include <scene/EntityRegistry.hpp>
#include <impl/scene/EntityStateService.hpp>
#include <data/EntityConversion.hpp>
#include <events/EventDispatcher.hpp>
#include <events/scene/EntityTransformEvents.hpp>
#include <entt/entt.hpp>
#include <optional>
#include <vector>

// ============================================================
// VK-1490: EntityStateService central selection normalization.
//
// Every selection write funnels through setSelectedEntities, which must prune
// invalid / destroyed / duplicate handles while preserving order (first
// occurrence wins; front() = active entity). Uses the real shared EnTT
// registry (ECSRegistry DLL) — entities are created per case and destroyed
// afterwards so the shared registry stays clean for other TUs.
//
// The service is constructed with a null SceneGraphSystem: the selection
// setters and queries never touch it.
// ============================================================

namespace
{
    services::EntityStateService& testService()
    {
        static services::EntityStateService service{nullptr};
        static bool registered = false;
        if (!registered)
        {
            registered = true;
            service.registerEventHandlers(events::EventDispatcher::instance());
        }
        return service;
    }

    struct TestEntities
    {
        entt::registry& registry = scene::EntityRegistry::getRegistry();
        std::vector<entt::entity> entities;

        services::EntityHandle create()
        {
            entities.push_back(registry.create());
            return services::internal::toHandle(entities.back());
        }

        ~TestEntities()
        {
            for (entt::entity e : entities)
            {
                if (registry.valid(e)) registry.destroy(e);
            }
        }
    };

    std::vector<uint64_t> idsOf(const std::vector<services::EntityHandle>& handles)
    {
        std::vector<uint64_t> result;
        for (const auto& h : handles) result.push_back(h.id);
        return result;
    }
}

TEST_SUITE("SelectionModel") {

TEST_CASE("duplicates are removed on write, first occurrence wins") {
    TestEntities fixture;
    auto& service = testService();
    const auto a = fixture.create();
    const auto b = fixture.create();
    const auto c = fixture.create();

    service.setSelectedEntities({a, b, a, c, b});
    CHECK(idsOf(service.getSelectedEntities()) ==
          std::vector<uint64_t>{a.id, b.id, c.id});

    service.setSelectedEntity(std::nullopt);
}

TEST_CASE("sentinel-invalid handles are dropped") {
    TestEntities fixture;
    auto& service = testService();
    const auto a = fixture.create();

    service.setSelectedEntities({services::EntityHandle::invalid(), a});
    CHECK(idsOf(service.getSelectedEntities()) == std::vector<uint64_t>{a.id});

    service.setSelectedEntity(std::nullopt);
}

TEST_CASE("destroyed-entity handles are dropped and the survivor becomes active") {
    TestEntities fixture;
    auto& service = testService();
    const auto dead = fixture.create();
    const auto alive = fixture.create();
    fixture.registry.destroy(services::internal::fromHandle(dead));

    service.setSelectedEntities({dead, alive});
    CHECK(idsOf(service.getSelectedEntities()) == std::vector<uint64_t>{alive.id});
    REQUIRE(service.getSelectedEntity().has_value());
    CHECK(service.getSelectedEntity()->id == alive.id);

    service.setSelectedEntity(std::nullopt);
}

TEST_CASE("front() after normalization is the active entity") {
    TestEntities fixture;
    auto& service = testService();
    const auto a = fixture.create();
    const auto b = fixture.create();

    service.setSelectedEntities({b, a});
    REQUIRE(service.getSelectedEntity().has_value());
    CHECK(service.getSelectedEntity()->id == b.id);

    service.setSelectedEntity(std::nullopt);
}

TEST_CASE("setSelectedEntity(nullopt) clears; a dead single select clears too") {
    TestEntities fixture;
    auto& service = testService();
    const auto a = fixture.create();

    service.setSelectedEntity(a);
    CHECK(service.getSelectedEntities().size() == 1);

    service.setSelectedEntity(std::nullopt);
    CHECK(service.getSelectedEntities().empty());
    CHECK_FALSE(service.getSelectedEntity().has_value());

    const auto dead = fixture.create();
    fixture.registry.destroy(services::internal::fromHandle(dead));
    service.setSelectedEntity(dead);
    CHECK(service.getSelectedEntities().empty());
}

TEST_CASE("EntitySelectedNotification carries the new primary entity") {
    TestEntities fixture;
    auto& service = testService();
    auto& dispatcher = events::EventDispatcher::instance();
    const auto a = fixture.create();
    const auto b = fixture.create();

    std::optional<services::EntityHandle> lastNotified;
    bool notified = false;
    auto token = dispatcher.subscribe<events::scene::EntitySelectedNotification>(
        [&](const events::scene::EntitySelectedNotification& n)
        {
            lastNotified = n.entity;
            notified = true;
        });

    service.setSelectedEntities({a, b});
    CHECK(notified);
    REQUIRE(lastNotified.has_value());
    CHECK(lastNotified->id == a.id);

    notified = false;
    service.setSelectedEntity(std::nullopt);
    CHECK(notified);
    CHECK_FALSE(lastNotified.has_value());

    dispatcher.unsubscribe(token);
}

TEST_CASE("CQRS events round-trip unchanged (backward compatibility)") {
    TestEntities fixture;
    auto& dispatcher = events::EventDispatcher::instance();
    testService(); // ensure handlers are registered
    const auto a = fixture.create();
    const auto b = fixture.create();

    events::scene::SelectEntitiesCommand multi;
    multi.entities = {a, b};
    dispatcher.execute(multi);
    CHECK(idsOf(dispatcher.query(events::scene::GetSelectedEntitiesQuery{})) ==
          std::vector<uint64_t>{a.id, b.id});

    events::scene::SelectEntityCommand single;
    single.entity = b;
    dispatcher.execute(single);
    auto selected = dispatcher.query(events::scene::GetSelectedEntityQuery{});
    REQUIRE(selected.has_value());
    CHECK(selected->id == b.id);
    CHECK(dispatcher.query(events::scene::GetSelectedEntitiesQuery{}).size() == 1);

    events::scene::SelectEntityCommand clear;
    clear.entity = std::nullopt;
    dispatcher.execute(clear);
    CHECK(dispatcher.query(events::scene::GetSelectedEntitiesQuery{}).empty());
}

TEST_CASE("selection is not implicitly pruned by entity destruction until next write") {
    // Documents the polling contract: consumers re-query each frame and writes
    // normalize; the stored vector itself is only rewritten on the next set.
    TestEntities fixture;
    auto& service = testService();
    const auto a = fixture.create();
    const auto b = fixture.create();

    service.setSelectedEntities({a, b});
    fixture.registry.destroy(services::internal::fromHandle(a));
    CHECK(service.getSelectedEntities().size() == 2); // stale until next write

    service.setSelectedEntities(service.getSelectedEntities());
    CHECK(idsOf(service.getSelectedEntities()) == std::vector<uint64_t>{b.id});

    service.setSelectedEntity(std::nullopt);
}

} // TEST_SUITE
