#include "BehaviorTreePlayModeHandler.hpp"
#include "BehaviorTreeInitialValues.hpp"
#include "../../providers/ai/IBehaviorTreeProvider.hpp"
#include "../../events/editor/EditorModeEvents.hpp"
#include "../../data/EntityConversion.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "print/Log.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <variant>

namespace services
{
    namespace
    {
        std::string entityLabel(const entt::registry& registry, entt::entity entity)
        {
            if (const auto* name = registry.try_get<components::NameComponent>(entity))
                return "'" + name->name + "'";
            return "entity " + std::to_string(static_cast<uint32_t>(entity));
        }

        const behaviortree::BlackboardKeyDef* findBlackboardKey(const behaviortree::BehaviorTreeData& tree,
                                                                const std::string& key)
        {
            for (const auto& definition : tree.graph.blackboardKeys)
            {
                if (definition.name == key)
                    return &definition;
            }
            return nullptr;
        }

        // VK-1660: writes the component's per-entity blackboard start values into the runtime that just
        // attached. The tree is the expanded one the runtime ticks, so SubTree keys count. A value whose
        // key the tree lacks, whose type differs from the key's, or whose entity cannot be found is
        // skipped with one warning.
        void applyInitialValues(IBehaviorTreeProvider& provider, const entt::registry& registry, entt::entity entity,
                                const components::BehaviorTreeComponent& component)
        {
            const EntityHandle handle = internal::toHandle(entity);
            const std::string label = entityLabel(registry, entity);
            const behaviortree::BTRuntimeState state = provider.getRuntimeState(handle);
            if (!state.tree)
            {
                vfLogWarning("[BT] {}: {} blackboard start value(s) not applied - the runtime reports no tree",
                             label, component.initialValues.size());
                return;
            }

            for (const auto& initial : component.initialValues)
            {
                const behaviortree::BlackboardKeyDef* key = findBlackboardKey(*state.tree, initial.key);
                if (!key)
                {
                    vfLogWarning("[BT] {}: blackboard start value '{}' skipped - tree '{}' has no such key",
                                 label, initial.key, state.treePath);
                    continue;
                }

                const behaviortree::BlackboardValueType type = initialValueType(initial.value);
                if (key->type != type)
                {
                    vfLogWarning("[BT] {}: blackboard start value '{}' skipped - it is {}, the key is {}",
                                 label, initial.key, behaviortree::blackboardValueTypeToString(type),
                                 behaviortree::blackboardValueTypeToString(key->type));
                    continue;
                }

                const std::optional<behaviortree::BlackboardValue> value = resolveInitialValue(registry, initial.value);
                if (!value)
                {
                    const auto& ref = std::get<behaviortree::BlackboardEntityRef>(initial.value);
                    vfLogWarning("[BT] {}: blackboard start value '{}' skipped - entity '{}' (UUID {}) not found",
                                 label, initial.key, ref.name, ref.uuid);
                    continue;
                }
                provider.setBlackboardValue(handle, initial.key, *value);
            }
        }
    }

    BehaviorTreePlayModeHandler::BehaviorTreePlayModeHandler(IBehaviorTreeProvider* provider)
        : provider(provider)
    {
    }

    BehaviorTreePlayModeHandler::~BehaviorTreePlayModeHandler()
    {
        unsubscribeFromEvents();
    }

    void BehaviorTreePlayModeHandler::subscribeToEvents()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        editorModeChangedToken = dispatcher.subscribe<::events::editor::EditorModeChangedNotification>(
            [this](const ::events::editor::EditorModeChangedNotification& notification)
            {
                onEditorModeChanged(notification.previousMode, notification.currentMode);
            });
    }

    void BehaviorTreePlayModeHandler::unsubscribeFromEvents()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        if (editorModeChangedToken.isValid())
        {
            dispatcher.unsubscribe(editorModeChangedToken);
            editorModeChangedToken = {};
        }
    }

    void BehaviorTreePlayModeHandler::onEditorModeChanged(EditorMode previousMode, EditorMode currentMode)
    {
        if (previousMode == EditorMode::Edit && currentMode == EditorMode::Play)
        {
            enterPlayMode();
        }
        else if (previousMode == EditorMode::Play && currentMode == EditorMode::Edit)
        {
            exitPlayMode();
        }
    }

    void BehaviorTreePlayModeHandler::enterPlayMode()
    {
        if (!provider) return;

        attachAllBehaviorTrees();
    }

    void BehaviorTreePlayModeHandler::exitPlayMode()
    {
        if (!provider) return;

        provider->stopAll();

        // Detach all trees - walk entities and detach each
        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::BehaviorTreeComponent>();

        for (auto entity : view)
        {
            auto handle = internal::toHandle(entity);
            if (provider->hasTree(handle))
            {
                provider->detachTree(handle);
            }

            auto& bt = view.get<components::BehaviorTreeComponent>(entity);
            bt.isInitialized = false;
        }

        vfLogInfo("Behavior tree play mode stopped");
    }

    void BehaviorTreePlayModeHandler::attachAllBehaviorTrees()
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::BehaviorTreeComponent>();

        int attached = 0;
        for (auto entity : view)
        {
            auto& bt = view.get<components::BehaviorTreeComponent>(entity);
            if (!bt.behaviorTreeRef.isValid() || !bt.enabled)
            {
                continue;
            }

            // Skip inactive entities
            if (registry.all_of<components::NameComponent>(entity))
            {
                const auto& nameComp = registry.get<components::NameComponent>(entity);
                if (!nameComp.isActive)
                {
                    continue;
                }
            }

            auto handle = internal::toHandle(entity);

            if (provider->attachTree(handle, bt.behaviorTreeRef.resolve()))
            {
                bt.isInitialized = true;
                ++attached;
                if (!bt.initialValues.empty())
                    applyInitialValues(*provider, registry, entity, bt);
            }
        }

        if (attached > 0)
        {
            vfLogInfo("Behavior tree play mode started with {} trees", attached);
        }
    }
}
