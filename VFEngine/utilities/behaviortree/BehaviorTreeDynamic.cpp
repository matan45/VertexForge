#include "BehaviorTreeDynamic.hpp"
#include <algorithm>
#include <unordered_set>
#include <variant>

namespace behaviortree
{
    // getStringProperty was folded into the shared getNodeProperty<std::string> in BehaviorTreeTypes.hpp.

    std::string resolveDynamicSubtreePath(const BTNode& node,
                                          const Blackboard& blackboard,
                                          const std::unordered_map<std::string, std::string>& injections)
    {
        // 1. Blackboard selection key (string value) — highest priority, fully runtime-driven.
        const std::string selectionKey = getNodeProperty<std::string>(node, "selectionKey", std::string{});
        if (!selectionKey.empty() && blackboard.has(selectionKey))
        {
            const BlackboardValue value = blackboard.get(selectionKey);
            if (std::holds_alternative<std::string>(value))
            {
                const std::string& path = std::get<std::string>(value);
                if (!path.empty()) return path;
            }
        }

        // 2. External injection by tag (SetDynamicSubtree).
        const std::string injectionTag = getNodeProperty<std::string>(node, "injectionTag", std::string{});
        if (!injectionTag.empty())
        {
            auto it = injections.find(injectionTag);
            if (it != injections.end() && !it->second.empty())
            {
                return it->second;
            }
        }

        // 3. Authoring-time default.
        return getNodeProperty<std::string>(node, "defaultTreePath", std::string{});
    }

    void applyMappingsIn(const std::vector<BlackboardMapping>& mappings,
                         const Blackboard& parent,
                         Blackboard& child)
    {
        for (const auto& m : mappings)
        {
            if (m.direction != MappingDirection::In && m.direction != MappingDirection::InOut) continue;
            if (m.parentKey.empty() || m.childKey.empty()) continue;
            if (!parent.has(m.parentKey)) continue;
            child.set(m.childKey, parent.get(m.parentKey));
        }
    }

    void applyMappingsOut(const std::vector<BlackboardMapping>& mappings,
                          Blackboard& parent,
                          const Blackboard& child)
    {
        for (const auto& m : mappings)
        {
            if (m.direction != MappingDirection::Out && m.direction != MappingDirection::InOut) continue;
            if (m.parentKey.empty() || m.childKey.empty()) continue;
            if (!child.has(m.childKey)) continue;
            parent.set(m.parentKey, child.get(m.childKey));
        }
    }

    std::vector<uint32_t> computeActivePath(const BTGraph& graph,
                                            const std::unordered_map<uint32_t, BTNodeRuntime>& states,
                                            uint32_t rootId)
    {
        std::vector<uint32_t> path;
        if (rootId == 0) return path;

        auto rootIt = states.find(rootId);
        if (rootIt == states.end() || rootIt->second.lastStatus != BTNodeStatus::Running)
        {
            return path; // tree is not currently running
        }

        std::unordered_set<uint32_t> visited;
        uint32_t current = rootId;
        while (current != 0 && visited.insert(current).second)
        {
            const BTNode* node = graph.findNodeById(current);
            if (!node) break;
            path.push_back(current);

            uint32_t next = 0;
            for (const BTNode* child : graph.getChildren(current))
            {
                auto it = states.find(child->id);
                if (it != states.end() && it->second.lastStatus == BTNodeStatus::Running)
                {
                    next = child->id;
                    break;
                }
            }
            current = next;
        }
        return path;
    }

    void captureSnapshotCore(const BTGraph& graph,
                             const std::unordered_map<uint32_t, BTNodeRuntime>& states,
                             const Blackboard& blackboard,
                             uint32_t rootId,
                             BTRuntimeSnapshot& out)
    {
        out.nodeStatuses.clear();
        out.currentChildIndices.clear();
        out.elapsedTimes.clear();
        out.lastResults.clear();

        out.nodeStatuses.reserve(states.size());
        out.currentChildIndices.reserve(states.size());
        out.elapsedTimes.reserve(states.size());
        out.lastResults.reserve(states.size());

        for (const auto& [nodeId, state] : states)
        {
            out.nodeStatuses[nodeId] = state.lastStatus;
            out.currentChildIndices[nodeId] = state.currentChildIndex;
            out.elapsedTimes[nodeId] = state.elapsedTime;
            out.lastResults[nodeId] = state.lastCompletedStatus;
        }

        out.activePath = computeActivePath(graph, states, rootId);

        const auto& values = blackboard.getAll();
        out.blackboard.clear();
        out.blackboard.reserve(values.size());
        for (const auto& [key, value] : values)
        {
            out.blackboard.emplace_back(key, value);
        }
        std::sort(out.blackboard.begin(), out.blackboard.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
    }
}
