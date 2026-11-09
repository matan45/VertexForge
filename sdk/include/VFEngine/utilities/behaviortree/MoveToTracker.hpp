#pragma once

#include "BehaviorTreeRuntime.hpp"
#include "BehaviorTreeTypes.hpp"

#include <glm/glm.hpp>
#include <cstdint>

// VK-1660: the per-tick decision of the built-in MoveTo task, free of the engine so it is CPU-testable.
// The executor keeps one MoveToTrackerState per entity, resolves the target's CURRENT position every tick
// (a moving entity or a Vec3 key) and acts on the returned action.
namespace behaviortree
{
    enum class MoveToAction : uint8_t
    {
        None,             // keep going
        IssueDestination, // send the agent to the target's current position
        Stop              // stop the agent (arrived, with stopOnArrival)
    };

    struct MoveToTrackerState
    {
        bool issued = false;          // a destination was sent since the task (re)started
        glm::vec3 issuedTarget{0.0f}; // the last destination sent
    };

    struct MoveToStep
    {
        MoveToAction action = MoveToAction::None;
        BTNodeStatus status = BTNodeStatus::Running; // Success on arrival
    };

    // Advances one tick.
    // First tick (or nothing sent yet): issue the target, Running. Arrival is checked from the next tick on:
    // the agent only acts on a new destination after the tree has ticked.
    // Later ticks, following (repathDistance > 0): within arrivalDistance of the target where it is NOW ->
    // Success, plus Stop when stopOnArrival. Otherwise re-issue the target's position when it has moved
    // farther than repathDistance from the last issued destination, or when the agent already reached that
    // destination (the target drifted less than repathDistance) - so it never stands at a stale point.
    // Not following (repathDistance 0): the first destination is final; arrival is measured against it.
    // Distances are on the XZ plane when `horizontal` (navmesh agents), else 3D.
    MoveToStep stepMoveTo(MoveToTrackerState& state, const MoveToRequest& request,
                          const glm::vec3& ownerPosition, const glm::vec3& targetPosition,
                          bool isFirstTick, bool horizontal = true);
}
