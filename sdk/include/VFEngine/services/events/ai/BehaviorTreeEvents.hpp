#pragma once
#include "../EventTypes.hpp"
#include "../../data/EntityHandle.hpp"
#include "../../../utilities/behaviortree/BehaviorTreeTypes.hpp"
#include <string>
#include <optional>
#include <vector>

namespace events::ai {

    struct AttachBehaviorTreeCommand : ICommand<bool> {
        services::EntityHandle entity;
        std::string treePath;

        std::string_view getName() const override { return "AttachBehaviorTree"; }
    };

    struct DetachBehaviorTreeCommand : ICommand<> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "DetachBehaviorTree"; }
    };

    struct SetBehaviorTreeEnabledCommand : ICommand<> {
        services::EntityHandle entity;
        bool enabled;

        std::string_view getName() const override { return "SetBehaviorTreeEnabled"; }
    };

    struct ReloadBehaviorTreeAssetCommand : ICommand<> {
        std::string treePath;

        std::string_view getName() const override { return "ReloadBehaviorTreeAsset"; }
    };

    struct SetBlackboardValueCommand : ICommand<> {
        services::EntityHandle entity;
        std::string key;
        behaviortree::BlackboardValue value;

        std::string_view getName() const override { return "SetBlackboardValue"; }
    };

    struct GetBlackboardValueQuery : IQuery<behaviortree::BlackboardValue> {
        services::EntityHandle entity;
        std::string key;

        std::string_view getName() const override { return "GetBlackboardValue"; }
    };

    struct HasBehaviorTreeQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "HasBehaviorTree"; }
    };

    struct GetBehaviorTreePathQuery : IQuery<std::string> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetBehaviorTreePath"; }
    };

    struct IsBehaviorTreeEnabledQuery : IQuery<bool> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "IsBehaviorTreeEnabled"; }
    };

    struct GetBehaviorTreeStatusQuery : IQuery<std::string> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetBehaviorTreeStatus"; }
    };

    struct HasBlackboardKeyQuery : IQuery<bool> {
        services::EntityHandle entity;
        std::string key;

        std::string_view getName() const override { return "HasBlackboardKey"; }
    };

    // === Debug ===

    struct BTAttachedTreeInfo {
        services::EntityHandle entity;
        std::string treePath;
        std::string name;
    };

    struct GetAttachedBehaviorTreesQuery : IQuery<std::vector<BTAttachedTreeInfo>> {
        std::string_view getName() const override { return "GetAttachedBehaviorTrees"; }
    };

    struct SetTreeDebugTargetCommand : ICommand<> {
        services::EntityHandle entity; // invalid handle disables snapshot capture

        std::string_view getName() const override { return "SetTreeDebugTarget"; }
    };

    struct GetTreeRuntimeSnapshotQuery : IQuery<behaviortree::BTRuntimeSnapshot> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetTreeRuntimeSnapshot"; }
    };

    // === Dynamic subtree (VK-1457) ===

    // Bind an injection tag to a tree path for one entity's DynamicSubTree nodes; empty path clears it.
    struct SetDynamicSubtreeCommand : ICommand<> {
        services::EntityHandle entity;
        std::string tag;
        std::string treePath;

        std::string_view getName() const override { return "SetDynamicSubtree"; }
    };

    // === Debug controls (VK-1457, editor-session only) ===

    struct SetTreeDebugPausedCommand : ICommand<> {
        bool paused = false;

        std::string_view getName() const override { return "SetTreeDebugPaused"; }
    };

    struct StepTreeDebugCommand : ICommand<> {
        std::string_view getName() const override { return "StepTreeDebug"; }
    };

    struct SetTreeBreakpointsCommand : ICommand<> {
        services::EntityHandle entity;
        std::vector<uint32_t> nodeIds;

        std::string_view getName() const override { return "SetTreeBreakpoints"; }
    };

}
