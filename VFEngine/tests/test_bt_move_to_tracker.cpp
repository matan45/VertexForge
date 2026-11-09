#include <doctest.h>
#include <behaviortree/MoveToTracker.hpp>
#include <glm/glm.hpp>

// ============================================================
// VK-1660: MoveTo per-tick decision (behaviortree::stepMoveTo): first-tick issue with deferred
// arrival, arrival (+ optional stop), and re-pathing after a moving target.
// ============================================================

namespace
{
    namespace bt = behaviortree;

    bt::MoveToRequest request(float arrival, float repath, bool stopOnArrival)
    {
        bt::MoveToRequest r;
        r.arrivalDistance = arrival;
        r.repathDistance = repath;
        r.stopOnArrival = stopOnArrival;
        return r;
    }
}

TEST_SUITE("BTMoveToTracker")
{
    TEST_CASE("the first tick issues the target and defers arrival")
    {
        bt::MoveToTrackerState state;
        const glm::vec3 target(5.0f, 0.0f, 0.0f);

        // Already standing on the target: still Running on the first tick (arrival deferred).
        const auto first = bt::stepMoveTo(state, request(0.5f, 1.0f, true), target, target, true);
        CHECK(first.action == bt::MoveToAction::IssueDestination);
        CHECK(first.status == bt::BTNodeStatus::Running);
        CHECK(state.issued);
        CHECK(state.issuedTarget == target);

        const auto second = bt::stepMoveTo(state, request(0.5f, 1.0f, true), target, target, false);
        CHECK(second.status == bt::BTNodeStatus::Success);
        CHECK(second.action == bt::MoveToAction::Stop);
    }

    TEST_CASE("arrival without stopOnArrival succeeds without stopping")
    {
        bt::MoveToTrackerState state;
        const glm::vec3 target(0.0f, 0.0f, 10.0f);
        bt::stepMoveTo(state, request(1.5f, 1.0f, false), glm::vec3(0.0f), target, true);

        const auto step = bt::stepMoveTo(state, request(1.5f, 1.0f, false), glm::vec3(0.0f, 0.0f, 9.0f), target, false);
        CHECK(step.status == bt::BTNodeStatus::Success);
        CHECK(step.action == bt::MoveToAction::None);
    }

    TEST_CASE("arrival is measured on the XZ plane unless horizontal is off")
    {
        const glm::vec3 target(0.0f, 0.0f, 0.0f);
        const glm::vec3 owner(0.2f, 5.0f, 0.0f); // 5 m below/above, 0.2 m away on XZ

        bt::MoveToTrackerState flat;
        bt::stepMoveTo(flat, request(0.5f, 1.0f, true), owner, target, true);
        CHECK(bt::stepMoveTo(flat, request(0.5f, 1.0f, true), owner, target, false).status == bt::BTNodeStatus::Success);

        bt::MoveToTrackerState spatial;
        bt::stepMoveTo(spatial, request(0.5f, 1.0f, true), owner, target, true, false);
        CHECK(bt::stepMoveTo(spatial, request(0.5f, 1.0f, true), owner, target, false, false).status ==
              bt::BTNodeStatus::Running);
    }

    TEST_CASE("a target that moved past repathDistance is re-issued")
    {
        bt::MoveToTrackerState state;
        const glm::vec3 owner(0.0f);
        bt::stepMoveTo(state, request(0.5f, 1.0f, true), owner, glm::vec3(10.0f, 0.0f, 0.0f), true);

        // Moved 0.8 m: within repathDistance, keep the old destination.
        auto step = bt::stepMoveTo(state, request(0.5f, 1.0f, true), owner, glm::vec3(10.8f, 0.0f, 0.0f), false);
        CHECK(step.action == bt::MoveToAction::None);
        CHECK(step.status == bt::BTNodeStatus::Running);
        CHECK(state.issuedTarget == glm::vec3(10.0f, 0.0f, 0.0f));

        // Moved 1.5 m from the issued point: re-issue the new position.
        step = bt::stepMoveTo(state, request(0.5f, 1.0f, true), owner, glm::vec3(11.5f, 0.0f, 0.0f), false);
        CHECK(step.action == bt::MoveToAction::IssueDestination);
        CHECK(state.issuedTarget == glm::vec3(11.5f, 0.0f, 0.0f));

        // A vertical move does not count while horizontal.
        step = bt::stepMoveTo(state, request(0.5f, 1.0f, true), owner, glm::vec3(11.5f, 3.0f, 0.0f), false);
        CHECK(step.action == bt::MoveToAction::None);
    }

    TEST_CASE("reaching a stale destination walks on to where the target is now")
    {
        bt::MoveToTrackerState state;
        const auto r = request(0.5f, 1.0f, true);
        bt::stepMoveTo(state, r, glm::vec3(0.0f), glm::vec3(10.0f, 0.0f, 0.0f), true);

        // The target drifted 0.8 m (under repathDistance) and stopped; the agent reached the old point.
        const glm::vec3 target(10.8f, 0.0f, 0.0f);
        const auto step = bt::stepMoveTo(state, r, glm::vec3(10.0f, 0.0f, 0.0f), target, false);
        CHECK(step.action == bt::MoveToAction::IssueDestination);
        CHECK(step.status == bt::BTNodeStatus::Running);
        CHECK(state.issuedTarget == target);

        // Walking on: no further re-issue until it arrives.
        CHECK(bt::stepMoveTo(state, r, glm::vec3(10.2f, 0.0f, 0.0f), target, false).action == bt::MoveToAction::None);
        CHECK(bt::stepMoveTo(state, r, glm::vec3(10.5f, 0.0f, 0.0f), target, false).status ==
              bt::BTNodeStatus::Success);
    }

    TEST_CASE("an unreachable stationary target is not re-issued every tick")
    {
        bt::MoveToTrackerState state;
        const auto r = request(0.5f, 1.0f, true);
        const glm::vec3 target(10.0f, 0.0f, 3.0f); // e.g. on a crate, 3 m off the navmesh
        bt::stepMoveTo(state, r, glm::vec3(0.0f), target, true);

        // The agent stopped at the nearest navmesh point, 3 m short: keep Running without re-issuing.
        const auto step = bt::stepMoveTo(state, r, glm::vec3(10.0f, 0.0f, 0.0f), target, false);
        CHECK(step.action == bt::MoveToAction::None);
        CHECK(step.status == bt::BTNodeStatus::Running);
    }

    TEST_CASE("repathDistance 0 never re-issues and arrives at the first destination")
    {
        bt::MoveToTrackerState state;
        const auto r = request(0.5f, 0.0f, true);
        bt::stepMoveTo(state, r, glm::vec3(0.0f), glm::vec3(10.0f, 0.0f, 0.0f), true);

        auto step = bt::stepMoveTo(state, r, glm::vec3(0.0f), glm::vec3(50.0f, 0.0f, 0.0f), false);
        CHECK(step.action == bt::MoveToAction::None);
        CHECK(step.status == bt::BTNodeStatus::Running);

        // The target kept moving; reaching the first destination still completes the move.
        step = bt::stepMoveTo(state, r, glm::vec3(10.2f, 0.0f, 0.0f), glm::vec3(50.0f, 0.0f, 0.0f), false);
        CHECK(step.status == bt::BTNodeStatus::Success);
        CHECK(step.action == bt::MoveToAction::Stop);
    }

    TEST_CASE("a lost record issues again even when it is not the first tick")
    {
        bt::MoveToTrackerState state; // e.g. cleared by a hot reload or a disable
        const auto step =
            bt::stepMoveTo(state, request(0.5f, 1.0f, true), glm::vec3(0.0f), glm::vec3(3.0f, 0.0f, 0.0f), false);
        CHECK(step.action == bt::MoveToAction::IssueDestination);
        CHECK(step.status == bt::BTNodeStatus::Running);
    }
}
