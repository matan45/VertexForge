#include <doctest.h>
#include "impl/ai/BehaviorTreeServiceImpl.hpp"
#include "impl/ai/BehaviorTreePlayModeHandler.hpp"
#include "impl/ai/BehaviorTreeInitialValues.hpp"
#include "providers/ai/IBehaviorTreeProvider.hpp"
#include "events/EventDispatcher.hpp"
#include "events/ai/BehaviorTreeComponentEvents.hpp"
#include "events/ai/BehaviorTreeEvents.hpp"
#include "events/editor/EditorModeEvents.hpp"
#include "data/EntityConversion.hpp"
#include "asset/AssetDatabase.hpp"
#include "asset/AssetRef.hpp"
#include "components/Components.hpp"
#include "scene/Entity.hpp"
#include "scene/EntityRegistry.hpp"

#include <entt/entt.hpp>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

// ============================================================
// VK-1660: BehaviorTreeComponent data events (per-entity blackboard start values stored as UUID + name)
// and BehaviorTreePlayModeHandler applying them at Edit->Play.
//
// UUIDs live in 916600-916699: scene::EntityRegistry is a process-global singleton shared by every test
// TU, so ranges must not collide.
// ============================================================

namespace
{
    namespace fs = std::filesystem;
    namespace bt = behaviortree;

    // A named entity with a fixed UUID that removes itself from the global registry at scope exit.
    class ScopedEntity
    {
    public:
        ScopedEntity(const std::string& name, uint64_t uuid)
        {
            scene::EntityRegistry::init(); // connect the UUID lookup hooks (idempotent)
            scene::Entity entity(name);
            entity.addOrReplaceComponent<components::UUIDComponent>(uuid);
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

    private:
        entt::entity handle{entt::null};
    };

    struct BlackboardWrite
    {
        uint64_t entity = 0;
        std::string key;
        bt::BlackboardValue value;
    };

    // Attaches only the entities listed in `attachable` (the registry is shared with other test TUs) and
    // reports `tree` as the tree every attached runtime ticks.
    struct FakeTreeProvider : services::IBehaviorTreeProvider
    {
        std::shared_ptr<const bt::BehaviorTreeData> tree;
        std::unordered_set<uint64_t> attachable;
        std::unordered_map<uint64_t, std::string> attached;
        std::vector<BlackboardWrite> writes;

        bool attachTree(services::EntityHandle entity, const std::string& treePath) override
        {
            if (!attachable.contains(entity.id)) return false;
            attached[entity.id] = treePath;
            return true;
        }
        void detachTree(services::EntityHandle entity) override { attached.erase(entity.id); }
        void setEnabled(services::EntityHandle, bool) override {}
        bool hasTree(services::EntityHandle entity) const override { return attached.contains(entity.id); }
        std::string getTreePath(services::EntityHandle entity) const override
        {
            auto it = attached.find(entity.id);
            return it == attached.end() ? std::string() : it->second;
        }
        std::vector<services::EntityHandle> getAttachedEntities() const override
        {
            std::vector<services::EntityHandle> result;
            for (const auto& entry : attached) result.push_back(services::EntityHandle{entry.first});
            return result;
        }
        bool isEnabled(services::EntityHandle entity) const override { return hasTree(entity); }
        std::string getStatus(services::EntityHandle entity) const override { return hasTree(entity) ? "running" : "stopped"; }
        void updateAll(float) override {}
        void stopAll() override {}
        void reloadAsset(const std::string&) override {}
        void setBlackboardValue(services::EntityHandle entity, const std::string& key,
                                const bt::BlackboardValue& value) override
        {
            writes.push_back(BlackboardWrite{entity.id, key, value});
        }
        bt::BlackboardValue getBlackboardValue(services::EntityHandle, const std::string&) override { return 0.0f; }
        bool hasBlackboardKey(services::EntityHandle, const std::string&) const override { return false; }
        void setDynamicSubtree(services::EntityHandle, const std::string&, const std::string&) override {}
        void setDebugTarget(services::EntityHandle) override {}
        bt::BTRuntimeSnapshot getRuntimeSnapshot(services::EntityHandle) const override { return {}; }
        void setDebugPaused(bool) override {}
        void stepDebug() override {}
        void setBreakpoints(services::EntityHandle, const std::vector<uint32_t>&) override {}

        bt::BTRuntimeState getRuntimeState(services::EntityHandle entity) const override
        {
            bt::BTRuntimeState state;
            auto it = attached.find(entity.id);
            if (it == attached.end()) return state;
            state.attached = true;
            state.enabled = true;
            state.treePath = it->second;
            state.status = "running";
            state.tree = tree;
            return state;
        }

        const BlackboardWrite* findWrite(uint64_t entity, const std::string& key) const
        {
            for (const auto& write : writes)
            {
                if (write.entity == entity && write.key == key) return &write;
            }
            return nullptr;
        }
    };

    // A clean dispatcher and asset database with a BehaviorTreeServiceImpl registered on a fake provider.
    struct ComponentDataFixture
    {
        FakeTreeProvider provider;
        services::BehaviorTreeServiceImpl service{&provider};

        ComponentDataFixture()
        {
            events::EventDispatcher::instance().clear();
            asset::AssetDatabase::instance().clear();
            scene::EntityRegistry::init();
            service.registerEventHandlers();
        }

        ~ComponentDataFixture()
        {
            events::EventDispatcher::instance().clear();
            asset::AssetDatabase::instance().clear();
        }

        ComponentDataFixture(const ComponentDataFixture&) = delete;
        ComponentDataFixture& operator=(const ComponentDataFixture&) = delete;

        static entt::registry& registry() { return scene::EntityRegistry::getRegistry(); }

        static std::optional<services::BehaviorTreeComponentData> get(services::EntityHandle entity)
        {
            events::ai::GetBehaviorTreeComponentDataQuery query;
            query.entity = entity;
            return events::EventDispatcher::instance().query(query);
        }

        static bool set(services::EntityHandle entity, services::BehaviorTreeComponentData data)
        {
            events::ai::SetBehaviorTreeComponentDataCommand cmd;
            cmd.entity = entity;
            cmd.data = std::move(data);
            return events::EventDispatcher::instance().execute(cmd);
        }
    };

    bt::BlackboardKeyDef keyDef(const std::string& name, bt::BlackboardValueType type, bt::BlackboardValue value)
    {
        bt::BlackboardKeyDef def;
        def.name = name;
        def.type = type;
        def.defaultValue = std::move(value);
        return def;
    }

    // The (expanded) tree the fake runtime reports: only its blackboard keys matter here.
    std::shared_ptr<const bt::BehaviorTreeData> makeGuardTree()
    {
        auto data = std::make_shared<bt::BehaviorTreeData>();
        data->name = "Guard";
        data->graph.blackboardKeys = {
            keyDef("chaseRange", bt::BlackboardValueType::Float, 10.0f),
            keyDef("hits", bt::BlackboardValueType::Int, int32_t{0}),
            keyDef("target", bt::BlackboardValueType::Entity, services::EntityHandle::invalid()),
            keyDef("ghost", bt::BlackboardValueType::Entity, services::EntityHandle::invalid()),
            keyDef("label", bt::BlackboardValueType::String, std::string("idle")),
        };
        return data;
    }
}

TEST_SUITE("BTComponentData")
{
    TEST_CASE_FIXTURE(ComponentDataFixture, "data events refuse entities without a BehaviorTreeComponent")
    {
        ScopedEntity plain("VK1660 Plain", 916600);
        CHECK_FALSE(get(plain.entityHandle()).has_value());
        CHECK_FALSE(get(services::EntityHandle::invalid()).has_value());
        CHECK_FALSE(set(plain.entityHandle(), {}));
        CHECK_FALSE(set(services::EntityHandle::invalid(), {}));
        CHECK_FALSE(registry().all_of<components::BehaviorTreeComponent>(plain.get()));
    }

    TEST_CASE_FIXTURE(ComponentDataFixture, "entity values are stored as UUID + name and resolved back to handles")
    {
        ScopedEntity guard("VK1660 Guard", 916601);
        ScopedEntity player("VK1660 Player", 916602);
        registry().emplace<components::BehaviorTreeComponent>(guard.get());

        services::BehaviorTreeComponentData data;
        data.enabled = false;
        data.initialValues = {
            {"target", player.entityHandle()},
            {"chaseRange", 15.0f},
            {"hits", int32_t{2}},
            {"angry", true},
            {"label", std::string("guard")},
            {"home", glm::vec3(1.0f, 2.0f, 3.0f)},
        };
        REQUIRE(set(guard.entityHandle(), data));

        const auto& stored = registry().get<components::BehaviorTreeComponent>(guard.get());
        CHECK_FALSE(stored.enabled);
        CHECK_FALSE(stored.behaviorTreeRef.isValid());
        REQUIRE(stored.initialValues.size() == 6);
        CHECK(stored.initialValues[0].key == "target");
        const auto* ref = std::get_if<bt::BlackboardEntityRef>(&stored.initialValues[0].value);
        REQUIRE(ref != nullptr);
        CHECK(ref->uuid == 916602u);
        CHECK(ref->name == "VK1660 Player");
        CHECK(std::get<float>(stored.initialValues[1].value) == doctest::Approx(15.0f));
        CHECK(std::get<int32_t>(stored.initialValues[2].value) == 2);
        CHECK(std::get<bool>(stored.initialValues[3].value) == true);
        CHECK(std::get<std::string>(stored.initialValues[4].value) == "guard");
        CHECK(std::get<glm::vec3>(stored.initialValues[5].value) == glm::vec3(1.0f, 2.0f, 3.0f));

        const auto back = get(guard.entityHandle());
        REQUIRE(back.has_value());
        CHECK(back->treePath.empty());
        CHECK_FALSE(back->enabled);
        REQUIRE(back->initialValues.size() == 6);
        CHECK(std::get<services::EntityHandle>(back->initialValues[0].value) == player.entityHandle());
        CHECK(back->initialValues[0].entityRef == bt::BlackboardEntityRef{916602, "VK1660 Player"});
        CHECK_FALSE(back->initialValues[0].unresolved);
        CHECK(std::get<float>(back->initialValues[1].value) == doctest::Approx(15.0f));
        CHECK(std::get<int32_t>(back->initialValues[2].value) == 2);
        CHECK(std::get<std::string>(back->initialValues[4].value) == "guard");
    }

    TEST_CASE_FIXTURE(ComponentDataFixture, "a UUID that no longer exists falls back to the entity's name")
    {
        ScopedEntity guard("VK1660 Guard", 916603);
        ScopedEntity twinA("VK1660 Twin", 916604);
        ScopedEntity twinB("VK1660 Twin", 916605);
        registry().emplace<components::BehaviorTreeComponent>(guard.get()).initialValues = {
            {"target", bt::BlackboardEntityRef{916690, "VK1660 Twin"}}, // e.g. re-minted by a prefab instance
        };

        // Duplicate names resolve to the lowest entity index, whatever the creation order.
        const entt::entity lowest =
            entt::to_entity(twinA.get()) < entt::to_entity(twinB.get()) ? twinA.get() : twinB.get();
        auto back = get(guard.entityHandle());
        REQUIRE(back.has_value());
        REQUIRE(back->initialValues.size() == 1);
        CHECK(std::get<services::EntityHandle>(back->initialValues[0].value) == services::internal::toHandle(lowest));
        CHECK_FALSE(back->initialValues[0].unresolved);

        // A UUID that still resolves wins over the name.
        registry().get<components::BehaviorTreeComponent>(guard.get()).initialValues = {
            {"target", bt::BlackboardEntityRef{916605, "VK1660 Twin"}},
        };
        back = get(guard.entityHandle());
        REQUIRE(back.has_value());
        CHECK(std::get<services::EntityHandle>(back->initialValues[0].value) == twinB.entityHandle());

        CHECK(services::resolveBlackboardEntityRef(registry(), bt::BlackboardEntityRef{}) ==
              services::EntityHandle::invalid());
    }

    TEST_CASE_FIXTURE(ComponentDataFixture, "an unresolved reference survives a Get -> Set round trip")
    {
        ScopedEntity guard("VK1660 Guard", 916606);
        const std::vector<bt::BlackboardInitialValue> original = {
            {"target", bt::BlackboardEntityRef{916691, "VK1660 Nobody"}},
            {"none", bt::BlackboardEntityRef{}},
        };
        registry().emplace<components::BehaviorTreeComponent>(guard.get()).initialValues = original;

        const auto data = get(guard.entityHandle());
        REQUIRE(data.has_value());
        REQUIRE(data->initialValues.size() == 2);
        CHECK(data->initialValues[0].unresolved);
        CHECK_FALSE(std::get<services::EntityHandle>(data->initialValues[0].value).isValid());
        CHECK(data->initialValues[0].entityRef == bt::BlackboardEntityRef{916691, "VK1660 Nobody"});
        CHECK_FALSE(data->initialValues[1].unresolved); // empty = no entity, not a missing one

        REQUIRE(set(guard.entityHandle(), *data));
        CHECK(registry().get<components::BehaviorTreeComponent>(guard.get()).initialValues == original);
    }

    TEST_CASE_FIXTURE(ComponentDataFixture, "the tree path is stored as an asset reference; other paths are refused")
    {
        const fs::path root = fs::temp_directory_path() / "vf_bt_component_data_tests";
        std::error_code ec;
        fs::remove_all(root, ec);
        fs::create_directories(root, ec);
        const fs::path treeFile = root / "Guard.vfBehaviorTree";
        {
            std::ofstream file(treeFile);
            file << "{}";
        }

        ScopedEntity guard("VK1660 Guard", 916607);
        registry().emplace<components::BehaviorTreeComponent>(guard.get());

        services::BehaviorTreeComponentData data;
        data.treePath = treeFile.string();
        REQUIRE(set(guard.entityHandle(), data));
        CHECK(registry().get<components::BehaviorTreeComponent>(guard.get()).behaviorTreeRef.isValid());
        const auto back = get(guard.entityHandle());
        REQUIRE(back.has_value());
        CHECK(back->treePath == asset::AssetDatabase::instance().resolveAssetPath(treeFile.string()));

        data.treePath = (root / "Patrol.mt").string();
        CHECK_FALSE(set(guard.entityHandle(), data));
        CHECK(get(guard.entityHandle())->treePath == back->treePath); // unchanged

        // A missing tree is refused before AssetRef::fromPath could register it (and write a .vfmeta).
        data.treePath = (root / "Missing.vfBehaviorTree").string();
        CHECK_FALSE(set(guard.entityHandle(), data));
        CHECK_FALSE(fs::exists(root / "Missing.vfBehaviorTree.vfmeta"));
        CHECK(get(guard.entityHandle())->treePath == back->treePath);

        data.treePath.clear();
        REQUIRE(set(guard.entityHandle(), data));
        CHECK(get(guard.entityHandle())->treePath.empty());

        fs::remove_all(root, ec);
    }

    TEST_CASE_FIXTURE(ComponentDataFixture, "GetBehaviorTreeRuntimeStateQuery reports the provider's runtime")
    {
        ScopedEntity guard("VK1660 Guard", 916608);
        provider.tree = makeGuardTree();
        provider.attachable.insert(guard.entityHandle().id);
        REQUIRE(provider.attachTree(guard.entityHandle(), "tests/Guard.vfBehaviorTree"));

        events::ai::GetBehaviorTreeRuntimeStateQuery query;
        query.entity = guard.entityHandle();
        const bt::BTRuntimeState state = events::EventDispatcher::instance().query(query);
        CHECK(state.attached);
        CHECK(state.treePath == "tests/Guard.vfBehaviorTree");
        CHECK(state.tree.get() == provider.tree.get());

        query.entity = services::EntityHandle::invalid();
        CHECK_FALSE(events::EventDispatcher::instance().query(query).attached);
    }

    TEST_CASE("Edit->Play writes each start value that matches the tree and skips the rest")
    {
        events::EventDispatcher::instance().clear();
        asset::AssetDatabase::instance().clear();
        scene::EntityRegistry::init();
        auto& registry = scene::EntityRegistry::getRegistry();

        ScopedEntity guard("VK1660 Guard", 916620);
        ScopedEntity hero("VK1660 Hero", 916621);
        ScopedEntity idle("VK1660 Idle", 916622);

        FakeTreeProvider provider;
        provider.tree = makeGuardTree();
        provider.attachable = {guard.entityHandle().id}; // idle has a tree too, but fails to attach

        {
            auto& component = registry.emplace<components::BehaviorTreeComponent>(guard.get());
            component.behaviorTreeRef =
                asset::AssetRef::fromGUIDAndPath(asset::AssetGUID::generate(), "tests/Guard.vfBehaviorTree");
            component.initialValues = {
                {"chaseRange", 15.0f},
                {"hits", int32_t{2}},
                {"target", bt::BlackboardEntityRef{916621, "VK1660 Hero"}},
                {"ghost", bt::BlackboardEntityRef{916699, "VK1660 Nobody"}}, // resolves to nothing: skipped
                {"missing", 1.0f},                                          // not a key of the tree: skipped
                {"label", int32_t{7}},                                      // the key is a String: skipped
            };
        }
        {
            auto& component = registry.emplace<components::BehaviorTreeComponent>(idle.get());
            component.behaviorTreeRef =
                asset::AssetRef::fromGUIDAndPath(asset::AssetGUID::generate(), "tests/Idle.vfBehaviorTree");
            component.initialValues = {{"chaseRange", 3.0f}};
        }

        services::BehaviorTreePlayModeHandler handler(&provider);
        handler.subscribeToEvents();

        auto changeMode = [](services::EditorMode from, services::EditorMode to)
        {
            events::editor::EditorModeChangedNotification notification;
            notification.previousMode = from;
            notification.currentMode = to;
            events::EventDispatcher::instance().publish(notification);
        };

        changeMode(services::EditorMode::Edit, services::EditorMode::Play);

        const uint64_t guardId = guard.entityHandle().id;
        REQUIRE(provider.attached.contains(guardId));
        CHECK(provider.attached.at(guardId) == "tests/Guard.vfBehaviorTree");
        CHECK_FALSE(provider.attached.contains(idle.entityHandle().id));
        CHECK(registry.get<components::BehaviorTreeComponent>(guard.get()).isInitialized);

        CHECK(provider.writes.size() == 3);
        const BlackboardWrite* chase = provider.findWrite(guardId, "chaseRange");
        REQUIRE(chase != nullptr);
        CHECK(std::get<float>(chase->value) == doctest::Approx(15.0f));
        const BlackboardWrite* hits = provider.findWrite(guardId, "hits");
        REQUIRE(hits != nullptr);
        CHECK(std::get<int32_t>(hits->value) == 2);
        const BlackboardWrite* target = provider.findWrite(guardId, "target");
        REQUIRE(target != nullptr);
        CHECK(std::get<services::EntityHandle>(target->value) == hero.entityHandle());
        CHECK(provider.findWrite(guardId, "ghost") == nullptr);
        CHECK(provider.findWrite(guardId, "missing") == nullptr);
        CHECK(provider.findWrite(guardId, "label") == nullptr);

        changeMode(services::EditorMode::Play, services::EditorMode::Edit);
        CHECK(provider.attached.empty());
        CHECK_FALSE(registry.get<components::BehaviorTreeComponent>(guard.get()).isInitialized);

        // A runtime that reports no tree gets no values (one warning instead).
        provider.tree.reset();
        changeMode(services::EditorMode::Edit, services::EditorMode::Play);
        CHECK(provider.attached.contains(guardId));
        CHECK(provider.writes.size() == 3);
        changeMode(services::EditorMode::Play, services::EditorMode::Edit);

        handler.unsubscribeFromEvents();
        events::EventDispatcher::instance().clear();
    }
}
