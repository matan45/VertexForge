#pragma once

#include "../../../utilities/behaviortree/BehaviorTreeTypes.hpp"
#include <functional>

namespace editor::windows
{
    using BTPropertyChangedCallback = std::function<void()>;

    class BTPropertyPanel
    {
    private:
        BTPropertyChangedCallback onPropertyChanged;
    public:
        void draw(behaviortree::BTNode* node, const behaviortree::BTGraph* graph);
        void setOnPropertyChanged(BTPropertyChangedCallback callback) { onPropertyChanged = std::move(callback); }

    private:

        void drawWaitProperties(behaviortree::BTNode& node);
        void drawLogProperties(behaviortree::BTNode& node);
        void drawMoveToProperties(behaviortree::BTNode& node, const behaviortree::BTGraph* graph);
        void drawPlayAnimationProperties(behaviortree::BTNode& node);
        void drawParallelProperties(behaviortree::BTNode& node);
        void drawRepeaterProperties(behaviortree::BTNode& node);
        void drawCooldownProperties(behaviortree::BTNode& node);
        void drawTimeLimitProperties(behaviortree::BTNode& node);
        void drawScriptTaskProperties(behaviortree::BTNode& node);
        void drawSetBlackboardProperties(behaviortree::BTNode& node, const behaviortree::BTGraph* graph);
        void drawCheckBlackboardProperties(behaviortree::BTNode& node, const behaviortree::BTGraph* graph);
        void drawLineOfSightProperties(behaviortree::BTNode& node, const behaviortree::BTGraph* graph);
        void drawBlackboardConditionProperties(behaviortree::BTNode& node, const behaviortree::BTGraph* graph);
        void drawEnvironmentQueryProperties(behaviortree::BTNode& node, const behaviortree::BTGraph* graph);
        void drawSubTreeProperties(behaviortree::BTNode& node);
        void drawServiceProperties(behaviortree::BTNode& node, const behaviortree::BTGraph* graph);

        void notifyChanged();
    };
}
