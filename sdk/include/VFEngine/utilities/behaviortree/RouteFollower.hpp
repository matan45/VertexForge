#pragma once

#include "BehaviorTreeTypes.hpp"

#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

// VK-1660: the stepping logic of the built-in FollowRoute task, free of the engine so it is
// CPU-testable. The executor keeps one RouteFollowerState per (entity, node), gathers the route's
// marker positions, calls stepRoute every tick and acts on the returned action.
namespace behaviortree
{
    struct RouteFollowerSettings
    {
        RouteMode mode = RouteMode::Loop;
        float arrivalDistance = 1.0f; // a marker counts as reached within this distance
        float dwell = 0.0f;           // seconds to wait at each marker (0 = go straight on)
        bool startAtNearest = true;   // first entry starts at the nearest marker instead of marker 0
        float legTimeout = 30.0f;     // seconds of walking per leg before the task fails (0 = never)
        bool horizontal = true;       // XZ distance (navmesh agents); false = 3D (volumetric agents)
    };

    enum class RouteAction : uint8_t
    {
        None,             // keep going
        IssueDestination, // send the agent to points[targetIndex]
        Stop              // stop the agent (dwell begins, route finished, or failure)
    };

    struct RouteFollowerState
    {
        bool started = false;  // a start marker has been chosen
        int targetIndex = 0;   // marker being walked to (or dwelt at)
        int direction = 1;     // +1 / -1, PingPong only
        bool dwelling = false;
        float dwellTimer = 0.0f;
        float legTimer = 0.0f; // walking time on the current leg
        bool finished = false; // Once mode reached the last marker
    };

    struct RouteStep
    {
        RouteAction action = RouteAction::None;
        int targetIndex = 0;
        // Running while patrolling; Success when a Once route reached its last marker;
        // Failure when there are no points or a leg timed out.
        BTNodeStatus status = BTNodeStatus::Running;
    };

    // Index of the point nearest to `position` (XZ when `horizontal`); -1 when `points` is empty.
    int nearestPointIndex(const std::vector<glm::vec3>& points, const glm::vec3& position, bool horizontal);

    // Advances one tick.
    // `entering` = the node just (re)started: its first tick, or re-entry after an abort / a finished
    // run. First entry picks the start marker (nearest when startAtNearest, else 0) and issues it.
    // Re-entry of an unfinished route keeps its target, drops any dwell and re-issues the target
    // (the agent was elsewhere); re-entry of a finished Once route starts over like a first entry.
    // While walking: within arrivalDistance -> Once at its last marker: Success + Stop; dwell > 0:
    // dwell + Stop; else advance + IssueDestination. legTimeout exceeded -> Failure + Stop.
    // While dwelling: when the dwell elapses, advance + IssueDestination.
    // Advance: Loop wraps to 0; PingPong reverses at either end; Once moves to the next index.
    // A single-point Loop / PingPong route holds at its point after arriving (Running, no re-issue).
    // Empty `points`: Failure + Stop.
    RouteStep stepRoute(const std::vector<glm::vec3>& points, const RouteFollowerSettings& settings,
                        RouteFollowerState& state, const glm::vec3& ownerPosition, float deltaTime,
                        bool entering);
}
