#pragma once
#include "../EventTypes.hpp"
#include "../../data/BehaviorTreeAuthoringData.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>

// VK-1660 behavior-tree authoring (MCP P11).
//
// The validated, hash-guarded, atomic write path for .vfBehaviorTree assets (the BT editor keeps its
// own permissive save). Handled by services::BehaviorTreeAuthoringService (Editor only). Paths are
// absolute engine paths. MAIN THREAD ONLY.
namespace events::ai {

    // Reads a .vfBehaviorTree from disk (never the adapter's cache, which holds expanded trees).
    struct LoadBehaviorTreeAssetQuery : IQuery<services::BehaviorTreeAssetLoadResult> {
        std::string path;

        std::string_view getName() const override { return "LoadBehaviorTreeAsset"; }
    };

    // Validates and atomically writes a .vfBehaviorTree, then publishes AssetSavedNotification (the
    // asset database registers its GUID/.vfmeta; an open BT editor reloads, or shows a conflict when it
    // has unsaved edits) and queues a hot reload of every live runtime that uses the file.
    // Refused, writing nothing: a path that is not an absolute .vfBehaviorTree path; `createNew` with an
    // existing file; `expectedContentHash` set and the file missing or hashing differently; any Error
    // from validation::validateBehaviorTree (SubTree paths must exist) or catalog::validateStrict.
    struct SaveBehaviorTreeAssetCommand : ICommand<services::BehaviorTreeAssetSaveResult> {
        std::string path;
        behaviortree::BehaviorTreeData data;
        std::optional<uint64_t> expectedContentHash;
        bool createNew = false;
        // The strict per-node checks' scope (catalog::StrictContext::onlyNodes): unset = every node (a
        // new or replaced tree), a set = the nodes this edit created or changed (empty = none).
        std::optional<std::unordered_set<uint32_t>> strictNodes;

        std::string_view getName() const override { return "SaveBehaviorTreeAsset"; }
    };

}
