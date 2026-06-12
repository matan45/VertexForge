#include <doctest.h>
#include <components/Components.hpp>
#include <scene/Entity.hpp>
#include <scene/EntityRegistry.hpp>
#include <asset/AssetDatabase.hpp>
#include <asset/AssetGUID.hpp>
#include <serialization/PrefabSerialization.hpp>
#include <impl/components/UIComponentService.hpp>
#include <data/EntityConversion.hpp>
#include <events/EventDispatcher.hpp>
#include <events/scene/ScenePersistenceEvents.hpp>
#include <events/scene/EntityTransformEvents.hpp>
#include <events/ui/UIListViewEvents.hpp>
#include <entt/entt.hpp>
#include <filesystem>
#include <fstream>

// UIListView reconcile semantics: pool-first growth, shrink-to-pool,
// marker indices, template-driven instantiation, and the serialization
// skip for engine-managed item instances.
//
// The real LoadPrefab/DeleteEntity handlers live in ScenePersistenceService /
// HierarchyService (not constructed in the CPU-only test runner), so this TU
// registers minimal fakes once: LoadPrefab spawns a bare UIRect entity under
// the requested parent; DeleteEntity destroys the subtree.

namespace
{
    void attachChild(entt::registry& registry, entt::entity parent, entt::entity child)
    {
        registry.emplace_or_replace<components::ParentComponent>(child).parent = parent;
        auto* children = registry.try_get<components::ChildrenComponent>(parent);
        if (!children)
        {
            children = &registry.emplace<components::ChildrenComponent>(parent);
        }
        children->children.push_back(child);
    }

    void destroySubtree(entt::registry& registry, entt::entity entity)
    {
        if (!registry.valid(entity)) return;
        if (auto* children = registry.try_get<components::ChildrenComponent>(entity))
        {
            auto copy = children->children;
            for (entt::entity child : copy)
            {
                destroySubtree(registry, child);
            }
        }
        if (auto* parentComp = registry.try_get<components::ParentComponent>(entity))
        {
            if (registry.valid(parentComp->parent))
            {
                if (auto* siblings = registry.try_get<components::ChildrenComponent>(parentComp->parent))
                {
                    std::erase(siblings->children, entity);
                }
            }
        }
        registry.destroy(entity);
    }

    services::UIComponentService& testService()
    {
        static services::UIComponentService service{nullptr};
        static bool registered = false;
        if (!registered)
        {
            registered = true;
            auto& dispatcher = events::EventDispatcher::instance();
            service.registerEventHandlers(dispatcher);

            dispatcher.registerCommandHandler<events::scene::LoadPrefabCommand>(
                [](const events::scene::LoadPrefabCommand& cmd)
                    -> std::optional<services::EntityHandle>
                {
                    auto& registry = scene::EntityRegistry::getRegistry();
                    // The test "template" path must be non-empty; anything works.
                    if (cmd.filePath.empty()) return std::nullopt;

                    entt::entity item = registry.create();
                    auto& name = registry.emplace<components::NameComponent>(item);
                    name.name = "FakePrefabItem";
                    name.isActive = true;
                    registry.emplace<components::UIRectComponent>(item);

                    if (cmd.parent.has_value())
                    {
                        entt::entity parent = services::internal::fromHandle(*cmd.parent);
                        if (registry.valid(parent))
                        {
                            attachChild(registry, parent, item);
                        }
                    }
                    return services::internal::toHandle(item);
                });

            dispatcher.registerCommandHandler<events::scene::DeleteEntityCommand>(
                [](const events::scene::DeleteEntityCommand& cmd) -> bool
                {
                    auto& registry = scene::EntityRegistry::getRegistry();
                    entt::entity entity = services::internal::fromHandle(cmd.entity);
                    if (!registry.valid(entity)) return false;
                    destroySubtree(registry, entity);
                    return true;
                });
        }
        return service;
    }

    entt::entity makeListEntity(entt::registry& registry, services::UIComponentService& service)
    {
        entt::entity list = registry.create();
        registry.emplace<components::NameComponent>(list).name = "List";
        service.addUIListViewComponent(services::internal::toHandle(list));
        // Fake template ref: a valid GUID that resolves nowhere — the ops use
        // resolve() output as the LoadPrefab path, so give the component a
        // template via the test-resolvable path route instead.
        return list;
    }
}

TEST_SUITE("UIListView")
{
    TEST_CASE("add auto-equips rect + layout group")
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto& service = testService();

        entt::entity list = makeListEntity(registry, service);
        CHECK(registry.all_of<components::UIListViewComponent>(list));
        CHECK(registry.all_of<components::UIRectComponent>(list));
        CHECK(registry.all_of<components::UILayoutGroupComponent>(list));

        destroySubtree(registry, list);
    }

    TEST_CASE("grow instantiates marked items; shrink pools; regrow reuses the pool")
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto& service = testService();

        entt::entity list = makeListEntity(registry, service);
        auto& comp = registry.get<components::UIListViewComponent>(list);

        // Unresolvable template GUID -> reconcile treats the template as
        // missing and targets zero items (resolve() returns "")
        comp.itemTemplateRef = asset::AssetRef::fromHexString("00000000000000aa");
        comp.itemCount = 4;
        CHECK(service.reconcileUIListView(list));
        CHECK(comp.itemInstances.empty());

        // Register a temp prefab path with the AssetDatabase directly so the
        // GUID resolves deterministically (no project root needed in tests)
        namespace fs = std::filesystem;
        fs::path dir = fs::temp_directory_path() / "vf_listview_test";
        fs::create_directories(dir);
        fs::path prefabPath = dir / "Item.vfPrefab";
        {
            std::ofstream out(prefabPath);
            out << "{}";
        }
        asset::AssetGUID guid = asset::AssetGUID::generate();
        asset::AssetDatabase::instance().registerAssetWithGUID(
            guid, prefabPath.string(), resource::AssetType::Prefab);
        comp.itemTemplateRef = asset::AssetRef::fromGUID(guid);
        REQUIRE(comp.itemTemplateRef.isValid());
        REQUIRE(!comp.itemTemplateRef.resolve().empty());

        // Grow to 4
        CHECK(service.setUIListItemCount(services::internal::toHandle(list), 4));
        REQUIRE(comp.itemInstances.size() == 4);
        CHECK(comp.pool.empty());
        for (int i = 0; i < 4; ++i)
        {
            entt::entity item = comp.itemInstances[static_cast<size_t>(i)];
            REQUIRE(registry.valid(item));
            const auto* marker = registry.try_get<components::UIListItemComponent>(item);
            REQUIRE(marker != nullptr);
            CHECK(marker->listView == list);
            CHECK(marker->index == i);
            CHECK(registry.get<components::NameComponent>(item).isActive);
        }

        // Shrink to 1: surplus pooled (still alive, inactive)
        entt::entity pooledA = comp.itemInstances[3];
        CHECK(service.setUIListItemCount(services::internal::toHandle(list), 1));
        CHECK(comp.itemInstances.size() == 1);
        CHECK(comp.pool.size() == 3);
        CHECK(registry.valid(pooledA));
        CHECK_FALSE(registry.get<components::NameComponent>(pooledA).isActive);

        // Regrow to 3: pool reused, no new entities
        CHECK(service.setUIListItemCount(services::internal::toHandle(list), 3));
        CHECK(comp.itemInstances.size() == 3);
        CHECK(comp.pool.size() == 1);
        for (int i = 0; i < 3; ++i)
        {
            CHECK(registry.get<components::UIListItemComponent>(comp.itemInstances[i]).index == i);
            CHECK(registry.get<components::NameComponent>(comp.itemInstances[i]).isActive);
        }

        // Selection clamps on shrink
        service.setUIListSelectedIndex(services::internal::toHandle(list), 2);
        CHECK(comp.selectedIndex == 2);
        CHECK(service.setUIListItemCount(services::internal::toHandle(list), 2));
        CHECK(comp.selectedIndex == -1);

        destroySubtree(registry, list);
        fs::remove_all(dir);
    }

    TEST_CASE("serializeEntityTree skips engine-managed item instances")
    {
        auto& registry = scene::EntityRegistry::getRegistry();

        scene::Entity parent("ListRoot");
        scene::Entity authored("AuthoredChild");
        scene::Entity instance("ItemInstance");

        attachChild(registry, parent.getHandle(), authored.getHandle());
        attachChild(registry, parent.getHandle(), instance.getHandle());
        registry.emplace<components::UIListItemComponent>(instance.getHandle());

        auto json = serialization::PrefabSerialization::serializeEntityTree(parent);
        REQUIRE(json.contains("children"));
        REQUIRE(json["children"].size() == 1);
        CHECK(json["children"][0]["name"] == "AuthoredChild");

        destroySubtree(registry, parent.getHandle());
    }
}
