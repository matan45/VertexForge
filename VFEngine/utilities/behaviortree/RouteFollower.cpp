#include "RouteFollower.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace behaviortree
{
    namespace
    {
        float routeDistanceSquared(const glm::vec3& a, const glm::vec3& b, bool horizontal)
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

        // Next marker after `index` for the route's mode. PingPong flips `direction` at either end.
        int advanceIndex(int index, int count, RouteMode mode, int& direction)
        {
            switch (mode)
            {
            case RouteMode::Loop:
                return (index + 1) % count;
            case RouteMode::PingPong:
            {
                if (count < 2)
                {
                    return 0;
                }
                int next = index + direction;
                if (next >= count)
                {
                    direction = -1;
                    next = count - 2;
                }
                else if (next < 0)
                {
                    direction = 1;
                    next = 1;
                }
                return next;
            }
            case RouteMode::Once:
            default:
                return std::min(index + 1, count - 1);
            }
        }

        RouteStep makeStep(RouteAction action, const RouteFollowerState& state,
                           BTNodeStatus status = BTNodeStatus::Running)
        {
            RouteStep step;
            step.action = action;
            step.targetIndex = state.targetIndex;
            step.status = status;
            return step;
        }

        // Starts (or restarts) walking toward the current target: any dwell ends, the leg timer resets.
        RouteStep issueLeg(RouteFollowerState& state)
        {
            state.dwelling = false;
            state.dwellTimer = 0.0f;
            state.legTimer = 0.0f;
            return makeStep(RouteAction::IssueDestination, state);
        }
    }

    int nearestPointIndex(const std::vector<glm::vec3>& points, const glm::vec3& position, bool horizontal)
    {
        int best = -1;
        float bestDistance = std::numeric_limits<float>::max();
        for (size_t i = 0; i < points.size(); ++i)
        {
            const float distance = routeDistanceSquared(points[i], position, horizontal);
            if (distance < bestDistance)
            {
                bestDistance = distance;
                best = static_cast<int>(i);
            }
        }
        return best;
    }

    RouteStep stepRoute(const std::vector<glm::vec3>& points, const RouteFollowerSettings& settings,
                        RouteFollowerState& state, const glm::vec3& ownerPosition, float deltaTime,
                        bool entering)
    {
        const int count = static_cast<int>(points.size());
        if (count == 0)
        {
            return makeStep(RouteAction::Stop, state, BTNodeStatus::Failure);
        }

        // First entry, or a finished Once route started again: pick the start marker and walk to it.
        if (!state.started || (entering && state.finished))
        {
            state = RouteFollowerState{};
            state.started = true;
            state.targetIndex = settings.startAtNearest
                ? nearestPointIndex(points, ownerPosition, settings.horizontal)
                : 0;
            return issueLeg(state);
        }

        // The route lost markers while this follower was on it: stay on a valid index.
        state.targetIndex = std::clamp(state.targetIndex, 0, count - 1);

        // Re-entry of an unfinished route (after an abort or a failed leg): the agent was elsewhere,
        // so walk the current leg again and drop any dwell in progress.
        if (entering)
        {
            return issueLeg(state);
        }

        // A finished Once route ticked again without re-entering keeps reporting Success.
        if (state.finished)
        {
            return makeStep(RouteAction::None, state, BTNodeStatus::Success);
        }

        if (state.dwelling)
        {
            // A single-point Loop / PingPong route holds at its point.
            if (count == 1 && settings.mode != RouteMode::Once)
            {
                return makeStep(RouteAction::None, state);
            }
            state.dwellTimer += deltaTime;
            if (state.dwellTimer >= settings.dwell)
            {
                state.targetIndex = advanceIndex(state.targetIndex, count, settings.mode, state.direction);
                return issueLeg(state);
            }
            return makeStep(RouteAction::None, state);
        }

        // Walking toward points[targetIndex].
        const float arrival = std::max(settings.arrivalDistance, 0.0f);
        if (routeDistanceSquared(points[state.targetIndex], ownerPosition, settings.horizontal) <= arrival * arrival)
        {
            if (settings.mode == RouteMode::Once && state.targetIndex >= count - 1)
            {
                state.finished = true;
                state.dwelling = false;
                return makeStep(RouteAction::Stop, state, BTNodeStatus::Success);
            }
            // Arrive once, then hold (single point) or wait out the dwell.
            if (count == 1 || settings.dwell > 0.0f)
            {
                state.dwelling = true;
                state.dwellTimer = 0.0f;
                return makeStep(RouteAction::Stop, state);
            }
            state.targetIndex = advanceIndex(state.targetIndex, count, settings.mode, state.direction);
            return issueLeg(state);
        }

        state.legTimer += deltaTime;
        if (settings.legTimeout > 0.0f && state.legTimer >= settings.legTimeout)
        {
            return makeStep(RouteAction::Stop, state, BTNodeStatus::Failure);
        }
        return makeStep(RouteAction::None, state);
    }
}
