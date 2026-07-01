#pragma once

#include "BehaviorTreeTypes.hpp"
#include "BehaviorTreeRuntime.hpp" // BTNodeRuntime
#include "Blackboard.hpp"
#include <string>
#include <vector>
#include <unordered_map>

// VK-1457 — pure, engine-free helpers for dynamic subtrees and the runtime debugger.
// Kept out of the adapter (Core) so they are CPU-testable in Tests.exe, which links Utilities but not Core.
namespace behaviortree
{
    // Resolve which tree a DynamicSubTree node should run, in precedence order:
    //   1. blackboard[selectionKey]  (a string value — the most dynamic source)
    //   2. injections[injectionTag]  (external SetDynamicSubtree injection)
    //   3. defaultTreePath           (authoring-time fallback)
    // Returns an empty string when nothing is selected (caller should fail the node).
    std::string resolveDynamicSubtreePath(const BTNode& node,
                                          const Blackboard& blackboard,
                                          const std::unordered_map<std::string, std::string>& injections);

    // Copy parent -> child before a nested tick, for mappings with direction In or InOut.
    // Only copies keys the parent actually has; child keys not referenced stay private to the child.
    void applyMappingsIn(const std::vector<BlackboardMapping>& mappings,
                         const Blackboard& parent,
                         Blackboard& child);

    // Copy child -> parent after a nested tick, for mappings with direction Out or InOut.
    void applyMappingsOut(const std::vector<BlackboardMapping>& mappings,
                          Blackboard& parent,
                          const Blackboard& child);

    // The chain of node ids from the root down to the deepest currently-running leaf.
    // Empty when the root is not Running (tree idle/terminal this tick). For a Parallel with
    // multiple running children this follows the FIRST running child (a single representative spine;
    // every running node is still reported individually in BTRuntimeSnapshot::nodeStatuses).
    std::vector<uint32_t> computeActivePath(const BTGraph& graph,
                                            const std::unordered_map<uint32_t, BTNodeRuntime>& states,
                                            uint32_t rootId);

    // Assemble the per-node debug fields of a snapshot from immutable graph + live node states +
    // blackboard. Fills nodeStatuses / currentChildIndices / elapsedTimes / lastResults / activePath /
    // blackboard (sorted). Does NOT touch valid/tickIndex/paused/history/aborts — the adapter owns those.
    void captureSnapshotCore(const BTGraph& graph,
                             const std::unordered_map<uint32_t, BTNodeRuntime>& states,
                             const Blackboard& blackboard,
                             uint32_t rootId,
                             BTRuntimeSnapshot& out);
}
