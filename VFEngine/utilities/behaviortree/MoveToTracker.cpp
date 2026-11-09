#include "MoveToTracker.hpp"

#include <algorithm>

namespace behaviortree
{
    namespace
    {
        float moveDistanceSquared(const glm::vec3& a, const glm::vec3& b, bool horizontal)
        {
            const float dx = a.x - b.x;
            const float dz = a.z - b.z;
            if (horizontal)
            {
                return dx * dx + dz * dz;
            }
            const float dy = a.y - b.y;
            return dx * dx + dy * dy + dz * dz;
        }
    }

    MoveToStep stepMoveTo(MoveToTrackerState& state, const MoveToRequest& request,
                          const glm::vec3& ownerPosition, const glm::vec3& targetPosition,
                          bool isFirstTick, bool horizontal)
    {
        MoveToStep step;
        if (isFirstTick || !state.issued)
        {
            state.issued = true;
            state.issuedTarget = targetPosition;
            step.action = MoveToAction::IssueDestination;
            return step;
        }

        const float arrival = std::max(request.arrivalDistance, 0.0f);
        const float arrivalSquared = arrival * arrival;
        const bool following = request.repathDistance > 0.0f;
        const glm::vec3& goal = following ? targetPosition : state.issuedTarget;
        if (moveDistanceSquared(ownerPosition, goal, horizontal) <= arrivalSquared)
        {
            step.action = request.stopOnArrival ? MoveToAction::Stop : MoveToAction::None;
            step.status = BTNodeStatus::Success;
            return step;
        }

        if (following)
        {
            const bool targetMoved = moveDistanceSquared(targetPosition, state.issuedTarget, horizontal) >
                                     request.repathDistance * request.repathDistance;
            // Not at the target yet (checked above), so a reached destination is stale: walk on.
            const bool reachedIssued =
                moveDistanceSquared(ownerPosition, state.issuedTarget, horizontal) <= arrivalSquared;
            if (targetMoved || reachedIssued)
            {
                state.issuedTarget = targetPosition;
                step.action = MoveToAction::IssueDestination;
            }
        }
        return step;
    }
}
