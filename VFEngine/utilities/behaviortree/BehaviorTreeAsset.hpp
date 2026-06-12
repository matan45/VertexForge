#pragma once

#include "BehaviorTreeTypes.hpp"
#include <string_view>
#include <string>
#include <optional>
#include <functional>

namespace behaviortree
{
    inline constexpr const char* BT_FORMAT_VERSION = "1.1";

    class BehaviorTreeAsset
    {
    public:
        using TreeLoader = std::function<std::optional<BehaviorTreeData>(const std::string& path)>;

        static std::optional<BehaviorTreeData> load(std::string_view path);
        static bool save(std::string_view path, const BehaviorTreeData& data);
        static BehaviorTreeData createDefault(const std::string& name = "New Behavior Tree");

        // Inline every SubTree node by splicing the referenced graph in with
        // re-namespaced ids and merged blackboard keys (parent wins on collision).
        // Returns false on missing/cyclic references or depth overflow.
        // The loader indirection keeps expansion testable without disk access.
        static bool expandSubTrees(BehaviorTreeData& data, const TreeLoader& loader,
                                   int maxDepth = 8);

        // load() + expandSubTrees() — the runtime-facing load path.
        // The editor keeps using load() so SubTree nodes stay authorable.
        // outDependencies (optional) receives the normalized paths of every tree
        // that went into the expansion (including the root) — used by hot reload
        // to rebind parents when a referenced subtree asset changes.
        static std::optional<BehaviorTreeData> loadExpanded(std::string_view path,
                                                            std::vector<std::string>* outDependencies = nullptr);
    };
}
