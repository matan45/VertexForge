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
#include <events/ui/UIAuthoringEvents.hpp>
#include <impl/scene/ScenePersistenceService.hpp>
#include <scene/SceneGraphSystem.hpp>
#include <entt/entt.hpp>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

// UIListView reconcile semantics: pool-first growth, shrink-to-pool,
// marker indices, template-driven instantiation, and the serialization
// skip for engine-managed item instances.
//
// The real LoadPrefab/DeleteEntity handlers live in ScenePersistenceService /
// HierarchyService (not constructed in the CPU-only test runner), so this TU
// registers minimal fakes once: LoadPrefab spawns a bare UIRect entity under
// the requested parent; DeleteEntity destroys the subtree.
//
// VK-1659: ReconcileUIListViewsCommand rebuilds the list views of an
// instantiated subtree. A "nested" template (nestedListTemplates) makes the
// fake LoadPrefab give its item a list view of its own and reconcile it, the
// way ScenePersistenceService::loadPrefab does for a real prefab.

namespace
{
    // Template path (as AssetRef::resolve reports it) -> the GUID of the
    // template the list view inside each of its items uses.
    std::unordered_map<std::string, asset::AssetGUID>& nestedListTemplates()
    {
        static std::unordered_map<std::string, asset::AssetGUID> templates;
        return templates;
    }

    // A temp .vfPrefab registered with the AssetDatabase so its GUID resolves
    // (no project root in tests). Returns {guid, resolved path}.
    std::pair<asset::AssetGUID, std::string> registerTemplate(const std::filesystem::path& path)
    {
        std::filesystem::create_directories(path.parent_path());
        {
            std::ofstream out(path);
            out << "{}";
        }
        asset::AssetGUID guid = asset::AssetGUID::generate();
        asset::AssetDatabase::instance().registerAssetWithGUID(
            guid, path.string(), resource::AssetType::Prefab);
        return {guid, asset::AssetRef::fromGUID(guid).resolve()};
    }

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

                    // VK-1659: a nested template's item holds a list view of
                    // its own, built right away as the real loadPrefab does.
                    auto nested = nestedListTemplates().find(cmd.filePath);
                    if (nested != nestedListTemplates().end())
                    {
                        service.addUIListViewComponent(services::internal::toHandle(item));
                        auto& list = registry.get<components::UIListViewComponent>(item);
                        list.itemTemplateRef = asset::AssetRef::fromGUID(nested->second);
                        list.itemCount = 1;

                        events::ui::ReconcileUIListViewsCommand reconcile;
                        reconcile.root = services::internal::toHandle(item);
                        events::EventDispatcher::instance().execute(reconcile);
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

    // ---- VK-1659: ReconcileUIListViewsCommand ----

    TEST_CASE("ReconcileUIListViewsCommand builds every list view in a subtree")
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto& service = testService();
        auto& dispatcher = events::EventDispatcher::instance();
        namespace fs = std::filesystem;

        const fs::path dir = fs::temp_directory_path() / "vf_listview_subtree";
        const asset::AssetGUID rowGuid = registerTemplate(dir / "Row.vfPrefab").first;

        auto makeNamed = [&registry](const char* name, entt::entity parent)
        {
            entt::entity entity = registry.create();
            auto& nameComp = registry.emplace<components::NameComponent>(entity);
            nameComp.name = name;
            nameComp.isActive = true;
            if (parent != entt::null)
            {
                attachChild(registry, parent, entity);
            }
            return entity;
        };

        // As instantiated from JSON: the lists carry a template and a count but no items yet.
        entt::entity panel = makeNamed("Panel", entt::null);
        entt::entity outer = makeNamed("OuterList", panel);
        service.addUIListViewComponent(services::internal::toHandle(outer));
        entt::entity holder = makeNamed("Holder", panel);
        entt::entity inner = makeNamed("InnerList", holder);
        service.addUIListViewComponent(services::internal::toHandle(inner));
        {
            auto& outerComp = registry.get<components::UIListViewComponent>(outer);
            outerComp.itemTemplateRef = asset::AssetRef::fromGUID(rowGuid);
            outerComp.itemCount = 2;
            auto& innerComp = registry.get<components::UIListViewComponent>(inner);
            innerComp.itemTemplateRef = asset::AssetRef::fromGUID(rowGuid);
            innerComp.itemCount = 3;
        }

        events::ui::ReconcileUIListViewsCommand command;
        command.root = services::internal::toHandle(panel);
        CHECK(dispatcher.execute(command) == 2);
        CHECK(registry.get<components::UIListViewComponent>(outer).itemInstances.size() == 2);
        REQUIRE(registry.get<components::UIListViewComponent>(inner).itemInstances.size() == 3);
        for (entt::entity item : registry.get<components::UIListViewComponent>(inner).itemInstances)
        {
            const auto* marker = registry.try_get<components::UIListItemComponent>(item);
            REQUIRE(marker != nullptr);
            CHECK(marker->listView == inner);
        }

        // A second pass keeps the same items.
        const std::vector<entt::entity> firstItems = registry.get<components::UIListViewComponent>(inner).itemInstances;
        CHECK(dispatcher.execute(command) == 2);
        CHECK(registry.get<components::UIListViewComponent>(inner).itemInstances == firstItems);

        destroySubtree(registry, panel);
        fs::remove_all(dir);
    }

    TEST_CASE("a list whose item template holds a list of itself stops at the first repeat")
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto& service = testService();
        auto& dispatcher = events::EventDispatcher::instance();
        namespace fs = std::filesystem;

        const fs::path dir = fs::temp_directory_path() / "vf_listview_cycle";
        const auto loop = registerTemplate(dir / "Loop.vfPrefab");
        nestedListTemplates()[loop.second] = loop.first;  // every Loop item holds a Loop list

        entt::entity list = makeListEntity(registry, service);
        {
            auto& comp = registry.get<components::UIListViewComponent>(list);
            comp.itemTemplateRef = asset::AssetRef::fromGUID(loop.first);
            comp.itemCount = 1;
        }

        events::ui::ReconcileUIListViewsCommand command;
        command.root = services::internal::toHandle(list);
        CHECK(dispatcher.execute(command) == 1);

        const auto& built = registry.get<components::UIListViewComponent>(list);
        REQUIRE(built.itemInstances.size() == 1);
        const entt::entity item = built.itemInstances[0];
        REQUIRE(registry.all_of<components::UIListViewComponent>(item));
        // The item's own Loop list repeats the template being built: it is left empty.
        CHECK(registry.get<components::UIListViewComponent>(item).itemInstances.empty());

        nestedListTemplates().erase(loop.second);
        destroySubtree(registry, list);
        fs::remove_all(dir);
    }

    TEST_CASE("setUIListItemCount stops a template that holds a list of itself at the first repeat")
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto& service = testService();
        namespace fs = std::filesystem;

        // The guard lives in reconcileUIListView, so the direct entry points (item count, list data, template,
        // scene load) stop the cycle at the same level as ReconcileUIListViewsCommand.
        const fs::path dir = fs::temp_directory_path() / "vf_listview_cycle_direct";
        const auto loop = registerTemplate(dir / "DirectLoop.vfPrefab");
        nestedListTemplates()[loop.second] = loop.first;  // every DirectLoop item holds a DirectLoop list

        entt::entity list = makeListEntity(registry, service);
        registry.get<components::UIListViewComponent>(list).itemTemplateRef = asset::AssetRef::fromGUID(loop.first);

        CHECK(service.setUIListItemCount(services::internal::toHandle(list), 2));

        const auto& built = registry.get<components::UIListViewComponent>(list);
        REQUIRE(built.itemInstances.size() == 2);
        for (entt::entity item : built.itemInstances)
        {
            REQUIRE(registry.all_of<components::UIListViewComponent>(item));
            CHECK(registry.get<components::UIListViewComponent>(item).itemInstances.empty());
        }

        nestedListTemplates().erase(loop.second);
        destroySubtree(registry, list);
        fs::remove_all(dir);
    }

    TEST_CASE("nested item templates are built at most four levels deep")
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto& service = testService();
        auto& dispatcher = events::EventDispatcher::instance();
        namespace fs = std::filesystem;

        // Level1 items hold a Level2 list, ..., Level4 items a Level5 list: every template differs, so only
        // the depth cap ends the chain.
        const fs::path dir = fs::temp_directory_path() / "vf_listview_depth";
        std::vector<std::pair<asset::AssetGUID, std::string>> levels;
        for (int i = 1; i <= 5; ++i)
        {
            levels.push_back(registerTemplate(dir / ("Level" + std::to_string(i) + ".vfPrefab")));
        }
        for (size_t i = 0; i + 1 < levels.size(); ++i)
        {
            nestedListTemplates()[levels[i].second] = levels[i + 1].first;
        }

        // Four levels are built (the outermost list included); the fifth list stays empty.
        auto checkFourLevels = [&registry](entt::entity list)
        {
            entt::entity current = list;
            for (int level = 1; level <= 4; ++level)
            {
                const auto& comp = registry.get<components::UIListViewComponent>(current);
                REQUIRE(comp.itemInstances.size() == 1);
                current = comp.itemInstances[0];
                REQUIRE(registry.all_of<components::UIListViewComponent>(current));
            }
            CHECK(registry.get<components::UIListViewComponent>(current).itemInstances.empty());
        };

        entt::entity viaCommand = makeListEntity(registry, service);
        {
            auto& comp = registry.get<components::UIListViewComponent>(viaCommand);
            comp.itemTemplateRef = asset::AssetRef::fromGUID(levels[0].first);
            comp.itemCount = 1;
        }
        events::ui::ReconcileUIListViewsCommand command;
        command.root = services::internal::toHandle(viaCommand);
        CHECK(dispatcher.execute(command) == 1);
        checkFourLevels(viaCommand);

        // A direct entry point stops at the same depth.
        entt::entity direct = makeListEntity(registry, service);
        registry.get<components::UIListViewComponent>(direct).itemTemplateRef =
            asset::AssetRef::fromGUID(levels[0].first);
        CHECK(service.setUIListItemCount(services::internal::toHandle(direct), 1));
        checkFourLevels(direct);

        for (const auto& level : levels)
        {
            nestedListTemplates().erase(level.second);
        }
        destroySubtree(registry, viaCommand);
        destroySubtree(registry, direct);
        fs::remove_all(dir);
    }

    TEST_CASE("instantiateEntityFromJson builds the list items the JSON never carries")
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto& service = testService();  // the UI handlers + the fake LoadPrefab the reconcile uses
        namespace fs = std::filesystem;

        const fs::path dir = fs::temp_directory_path() / "vf_listview_json";
        const asset::AssetGUID rowGuid = registerTemplate(dir / "JsonRow.vfPrefab").first;

        scene::Entity source("JsonList");
        service.addUIListViewComponent(services::internal::toHandle(source.getHandle()));
        {
            auto& comp = registry.get<components::UIListViewComponent>(source.getHandle());
            comp.itemTemplateRef = asset::AssetRef::fromGUID(rowGuid);
            comp.itemCount = 3;
        }
        const std::string json = serialization::PrefabSerialization::serializeEntityTree(source).dump();

        // The real service (only the method; its handlers are not registered here).
        auto sceneGraph = std::make_shared<scene::SceneGraphSystem>();
        services::ScenePersistenceService persistence(sceneGraph, nullptr);
        const std::optional<services::EntityHandle> created =
            persistence.instantiateEntityFromJson(json, std::nullopt);
        REQUIRE(created.has_value());

        const entt::entity list = services::internal::fromHandle(*created);
        REQUIRE(registry.all_of<components::UIListViewComponent>(list));
        const auto& built = registry.get<components::UIListViewComponent>(list);
        CHECK(built.itemCount == 3);
        CHECK(built.itemInstances.size() == 3);
        for (entt::entity item : built.itemInstances)
        {
            CHECK(registry.all_of<components::UIListItemComponent>(item));
        }

        destroySubtree(registry, sceneGraph->GetRoot().getHandle());  // the scene root, the list and its items
        destroySubtree(registry, source.getHandle());
        fs::remove_all(dir);
    }
}
