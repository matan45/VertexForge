#pragma once

#include "../../data/EntityHandle.hpp"
#include "../../data/EditorMode.hpp"
#include "../../events/EventDispatcher.hpp"

namespace services
{
    class IBehaviorTreeProvider;

    class BehaviorTreePlayModeHandler
    {
    public:
        explicit BehaviorTreePlayModeHandler(IBehaviorTreeProvider* provider);
        ~BehaviorTreePlayModeHandler();

        BehaviorTreePlayModeHandler(const BehaviorTreePlayModeHandler&) = delete;
        BehaviorTreePlayModeHandler& operator=(const BehaviorTreePlayModeHandler&) = delete;

        void subscribeToEvents();
        void unsubscribeFromEvents();

    private:
        void onEditorModeChanged(EditorMode previousMode, EditorMode currentMode);
        void enterPlayMode();
        void exitPlayMode();
        void attachAllBehaviorTrees();

        IBehaviorTreeProvider* provider = nullptr;
        ::events::SubscriptionToken editorModeChangedToken;
    };
}
