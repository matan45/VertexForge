#pragma once

#include "BehaviorTreeTypes.hpp"
#include <string_view>
#include <string>
#include <optional>
#include <functional>

namespace behaviortree
{
    // 1.2 (VK-1457) adds DynamicSubTree nodes + per-node blackboardMappings.
    // Backward compatible: load() never gates on version; missing mappings deserialize to empty.
    inline constexpr const char* BT_FORMAT_VERSION = "1.2";

    class BehaviorTreeAsset
    {
    public:
        using TreeLoader = std::function<std::optional<BehaviorTreeData>(const std::string& path)>;

        static std::optional<BehaviorTreeData> load(std::string_view path);
        static bool save(std::string_view path, const BehaviorTreeData& data);
        static bool exists(std::string_view path);
        static BehaviorTreeData createDefault(const std::string& name = "New Behavior Tree");

        // Inline every SubTree node by splicing the referenced graph in with
        // re-namespaced ids and merged blackboard keys (parent wins on collision).
        // Returns false on missing/cyclic references or depth overflow.
        // The loader indirection keeps expansion testable without disk access.
        // outSubtreeEntryMap (optional, VK-1457) — see loadExpanded; populated for top-level SubTree nodes.
        static bool expandSubTrees(BehaviorTreeData& data, const TreeLoader& loader,
                                   int maxDepth = 8,
                                   std::unordered_map<uint32_t, uint32_t>* outSubtreeEntryMap = nullptr);

        // load() + expandSubTrees() — the runtime-facing load path.
        // The editor keeps using load() so SubTree nodes stay authorable.
        // outDependencies (optional) receives the normalized paths of every tree
        // that went into the expansion (including the root) — used by hot reload
        // to rebind parents when a referenced subtree asset changes.
        // outSubtreeEntryMap (optional, VK-1457) receives, for each TOP-LEVEL authored SubTree node id,
        // the expanded entry node id it was spliced into — lets the editor debugger map breakpoints and
        // live status onto SubTree nodes, which are removed from the runtime's expanded id space.
        static std::optional<BehaviorTreeData> loadExpanded(
            std::string_view path,
            std::vector<std::string>* outDependencies = nullptr,
            std::unordered_map<uint32_t, uint32_t>* outSubtreeEntryMap = nullptr);
    };
}
