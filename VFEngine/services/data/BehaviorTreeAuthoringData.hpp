#pragma once
#include <behaviortree/BehaviorTreeTypes.hpp>
#include <behaviortree/BehaviorTreeValidation.hpp>
#include <cstdint>
#include <string>
#include <vector>

// VK-1660: data carried by the behavior-tree authoring events (services/events/ai/
// BehaviorTreeAuthoringEvents.hpp). Header-only value types.
namespace services
{
    struct BehaviorTreeAssetLoadResult
    {
        bool success = false;
        std::string error;
        behaviortree::BehaviorTreeData data; // as authored: static SubTree nodes are not expanded
        uint64_t contentHash = 0;            // hash of the file bytes `data` was parsed from
    };

    struct BehaviorTreeAssetSaveResult
    {
        bool success = false;
        std::string error;        // why the save was refused (path, guard, validation or I/O)
        bool created = false;     // the file did not exist before this save
        uint64_t contentHash = 0; // hash of the bytes now on disk
        // validation::validateBehaviorTree plus catalog::validateStrict findings. A refused save lists
        // the Errors that blocked it; a written one keeps the Warnings.
        std::vector<behaviortree::validation::Diagnostic> diagnostics;
    };
}
