#include "BehaviorTreePlayModeHandler.hpp"
#include "../../providers/ai/IBehaviorTreeProvider.hpp"
#include "../../events/editor/EditorModeEvents.hpp"
#include "../../data/EntityConversion.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "print/Log.hpp"

namespace services
{
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
            if (bt.behaviorTreePath.empty() || !bt.enabled)
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

            if (provider->attachTree(handle, bt.behaviorTreePath))
            {
                bt.isInitialized = true;
                ++attached;
            }
        }

        if (attached > 0)
        {
            vfLogInfo("Behavior tree play mode started with {} trees", attached);
        }
    }
}
