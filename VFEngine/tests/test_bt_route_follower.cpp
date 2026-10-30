#include <doctest.h>
#include <behaviortree/RouteFollower.hpp>
#include <glm/glm.hpp>
#include <vector>

// ============================================================
// VK-1660: FollowRoute stepping logic (behaviortree::stepRoute).
// The agent is simulated: it teleports onto every issued marker, so each tick either
// arrives (advance / dwell / finish) or, when left in place, keeps walking.
// ============================================================

namespace
{
    namespace bt = behaviortree;

    std::vector<glm::vec3> line3()
    {
        return {glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(10.0f, 0.0f, 0.0f), glm::vec3(20.0f, 0.0f, 0.0f)};
    }

    // Drives a follower whose agent teleports onto every issued marker. Returns the issued marker
    // order (up to `issues` entries), stopping early when the step leaves Running.
    std::vector<int> visitOrder(const std::vector<glm::vec3>& points, const bt::RouteFollowerSettings& settings,
                                int issues, const glm::vec3& start, bt::BTNodeStatus* finalStatus = nullptr)
    {
        bt::RouteFollowerState state;
        glm::vec3 position = start;
        std::vector<int> order;
        bool entering = true;
        bt::BTNodeStatus status = bt::BTNodeStatus::Running;
        for (int tick = 0; tick < issues * 8 && static_cast<int>(order.size()) < issues; ++tick)
        {
            const bt::RouteStep step = bt::stepRoute(points, settings, state, position, 0.1f, entering);
            entering = false;
            if (step.action == bt::RouteAction::IssueDestination)
            {
                order.push_back(step.targetIndex);
                position = points[static_cast<size_t>(step.targetIndex)];
            }
            status = step.status;
            if (status != bt::BTNodeStatus::Running)
            {
                break;
            }
        }
        if (finalStatus)
        {
            *finalStatus = status;
        }
        return order;
    }
}

TEST_SUITE("BTRouteFollower")
{
    TEST_CASE("nearestPointIndex: empty is -1, horizontal ignores height")
    {
        CHECK(bt::nearestPointIndex({}, glm::vec3(0.0f), true) == -1);

        const std::vector<glm::vec3> points = {glm::vec3(0.0f, 10.0f, 0.0f), glm::vec3(3.0f, 0.0f, 0.0f)};
        CHECK(bt::nearestPointIndex(points, glm::vec3(0.0f), true) == 0);  // XZ: 0 vs 3
        CHECK(bt::nearestPointIndex(points, glm::vec3(0.0f), false) == 1); // 3D: 10 vs 3
    }

    TEST_CASE("first entry: nearest start vs marker 0")
    {
        const auto points = line3();
        const glm::vec3 owner(11.0f, 0.0f, 0.0f);

        bt::RouteFollowerSettings nearest;
        nearest.startAtNearest = true;
        bt::RouteFollowerState stateA;
        const auto stepA = bt::stepRoute(points, nearest, stateA, owner, 0.1f, true);
        CHECK(stepA.action == bt::RouteAction::IssueDestination);
        CHECK(stepA.targetIndex == 1);
        CHECK(stepA.status == bt::BTNodeStatus::Running);

        bt::RouteFollowerSettings fromStart;
        fromStart.startAtNearest = false;
        bt::RouteFollowerState stateB;
        const auto stepB = bt::stepRoute(points, fromStart, stateB, owner, 0.1f, true);
        CHECK(stepB.action == bt::RouteAction::IssueDestination);
        CHECK(stepB.targetIndex == 0);
    }

    TEST_CASE("first entry: horizontal vs 3D nearest, and arrival distance")
    {
        const std::vector<glm::vec3> points = {glm::vec3(0.0f, 10.0f, 0.0f), glm::vec3(3.0f, 0.0f, 0.0f)};

        bt::RouteFollowerSettings flat;
        flat.horizontal = true;
        bt::RouteFollowerState flatState;
        CHECK(bt::stepRoute(points, flat, flatState, glm::vec3(0.0f), 0.1f, true).targetIndex == 0);
        // Directly below marker 0: arrived in XZ, so it advances to marker 1.
        const auto flatNext = bt::stepRoute(points, flat, flatState, glm::vec3(0.0f), 0.1f, false);
        CHECK(flatNext.action == bt::RouteAction::IssueDestination);
        CHECK(flatNext.targetIndex == 1);

        bt::RouteFollowerSettings volume;
        volume.horizontal = false;
        volume.startAtNearest = false;
        bt::RouteFollowerState volumeState;
        CHECK(bt::stepRoute(points, volume, volumeState, glm::vec3(0.0f), 0.1f, true).targetIndex == 0);
        // 10 m below marker 0 in 3D: not arrived, keeps walking.
        const auto volumeNext = bt::stepRoute(points, volume, volumeState, glm::vec3(0.0f), 0.1f, false);
        CHECK(volumeNext.action == bt::RouteAction::None);
        CHECK(volumeNext.targetIndex == 0);
        CHECK(volumeNext.status == bt::BTNodeStatus::Running);
    }

    TEST_CASE("Loop wraps to marker 0")
    {
        bt::RouteFollowerSettings settings;
        settings.mode = bt::RouteMode::Loop;
        settings.startAtNearest = false;
        const auto order = visitOrder(line3(), settings, 7, glm::vec3(-5.0f, 0.0f, 0.0f));
        CHECK(order == std::vector<int>{0, 1, 2, 0, 1, 2, 0});
    }

    TEST_CASE("PingPong reverses at both ends")
    {
        bt::RouteFollowerSettings settings;
        settings.mode = bt::RouteMode::PingPong;
        settings.startAtNearest = false;
        const auto order = visitOrder(line3(), settings, 8, glm::vec3(-5.0f, 0.0f, 0.0f));
        CHECK(order == std::vector<int>{0, 1, 2, 1, 0, 1, 2, 1});
    }

    TEST_CASE("PingPong starting at the last marker turns back")
    {
        bt::RouteFollowerSettings settings;
        settings.mode = bt::RouteMode::PingPong;
        settings.startAtNearest = true;
        const auto order = visitOrder(line3(), settings, 5, glm::vec3(25.0f, 0.0f, 0.0f));
        CHECK(order == std::vector<int>{2, 1, 0, 1, 2});
    }

    TEST_CASE("Once succeeds at the last marker and restarts on re-entry")
    {
        const auto points = line3();
        bt::RouteFollowerSettings settings;
        settings.mode = bt::RouteMode::Once;
        settings.startAtNearest = true;

        bt::BTNodeStatus status = bt::BTNodeStatus::Running;
        const auto order = visitOrder(points, settings, 10, glm::vec3(9.0f, 0.0f, 0.0f), &status);
        CHECK(order == std::vector<int>{1, 2});
        CHECK(status == bt::BTNodeStatus::Success);

        // Drive it by hand to inspect the finished state.
        bt::RouteFollowerState state;
        glm::vec3 position(9.0f, 0.0f, 0.0f);
        auto step = bt::stepRoute(points, settings, state, position, 0.1f, true);
        REQUIRE(step.targetIndex == 1);
        position = points[1];
        step = bt::stepRoute(points, settings, state, position, 0.1f, false);
        REQUIRE(step.targetIndex == 2);
        position = points[2];
        step = bt::stepRoute(points, settings, state, position, 0.1f, false);
        CHECK(step.action == bt::RouteAction::Stop);
        CHECK(step.status == bt::BTNodeStatus::Success);
        CHECK(state.finished);

        // Ticked again without re-entering: still Success, nothing issued.
        step = bt::stepRoute(points, settings, state, position, 0.1f, false);
        CHECK(step.action == bt::RouteAction::None);
        CHECK(step.status == bt::BTNodeStatus::Success);

        // Re-entered: starts over from the nearest marker (2, where the agent stands).
        step = bt::stepRoute(points, settings, state, position, 0.1f, true);
        CHECK(step.action == bt::RouteAction::IssueDestination);
        CHECK(step.targetIndex == 2);
        CHECK(step.status == bt::BTNodeStatus::Running);
        CHECK_FALSE(state.finished);
    }

    TEST_CASE("dwell stops, waits, then issues the next marker")
    {
        const auto points = line3();
        bt::RouteFollowerSettings settings;
        settings.startAtNearest = false;
        settings.dwell = 0.25f;

        bt::RouteFollowerState state;
        glm::vec3 position = points[0];
        auto step = bt::stepRoute(points, settings, state, position, 0.1f, true);
        CHECK(step.action == bt::RouteAction::IssueDestination);
        CHECK(step.targetIndex == 0);

        step = bt::stepRoute(points, settings, state, position, 0.1f, false); // arrives
        CHECK(step.action == bt::RouteAction::Stop);
        CHECK(step.status == bt::BTNodeStatus::Running);
        CHECK(state.dwelling);

        step = bt::stepRoute(points, settings, state, position, 0.1f, false); // 0.1 s
        CHECK(step.action == bt::RouteAction::None);
        step = bt::stepRoute(points, settings, state, position, 0.1f, false); // 0.2 s
        CHECK(step.action == bt::RouteAction::None);
        step = bt::stepRoute(points, settings, state, position, 0.1f, false); // 0.3 s >= 0.25
        CHECK(step.action == bt::RouteAction::IssueDestination);
        CHECK(step.targetIndex == 1);
        CHECK_FALSE(state.dwelling);
    }

    TEST_CASE("leg timeout fails the task; 0 disables it")
    {
        const auto points = line3();
        const glm::vec3 stuck(-50.0f, 0.0f, 0.0f);

        bt::RouteFollowerSettings settings;
        settings.startAtNearest = false;
        settings.legTimeout = 0.5f;
        bt::RouteFollowerState state;
        auto step = bt::stepRoute(points, settings, state, stuck, 0.25f, true);
        CHECK(step.action == bt::RouteAction::IssueDestination);
        step = bt::stepRoute(points, settings, state, stuck, 0.25f, false); // 0.25 s
        CHECK(step.status == bt::BTNodeStatus::Running);
        step = bt::stepRoute(points, settings, state, stuck, 0.25f, false); // 0.5 s
        CHECK(step.status == bt::BTNodeStatus::Failure);
        CHECK(step.action == bt::RouteAction::Stop);
        CHECK(step.targetIndex == 0);

        // Re-entered after the failure: keeps its target and re-issues it.
        step = bt::stepRoute(points, settings, state, stuck, 0.25f, true);
        CHECK(step.action == bt::RouteAction::IssueDestination);
        CHECK(step.targetIndex == 0);
        CHECK(step.status == bt::BTNodeStatus::Running);

        bt::RouteFollowerSettings noTimeout = settings;
        noTimeout.legTimeout = 0.0f;
        bt::RouteFollowerState patient;
        bt::stepRoute(points, noTimeout, patient, stuck, 1.0f, true);
        for (int tick = 0; tick < 100; ++tick)
        {
            step = bt::stepRoute(points, noTimeout, patient, stuck, 1.0f, false);
            REQUIRE(step.status == bt::BTNodeStatus::Running);
            REQUIRE(step.action == bt::RouteAction::None);
        }
    }

    TEST_CASE("re-entry after an abort keeps the target and clears a dwell")
    {
        const auto points = line3();
        bt::RouteFollowerSettings settings;
        settings.startAtNearest = false;
        settings.dwell = 1.0f;

        bt::RouteFollowerState state;
        bt::stepRoute(points, settings, state, points[0], 0.1f, true);
        auto step = bt::stepRoute(points, settings, state, points[0], 0.1f, false); // arrives, dwells
        REQUIRE(state.dwelling);
        REQUIRE(step.action == bt::RouteAction::Stop);

        // The node was aborted (e.g. a chase) and the agent is now far away.
        step = bt::stepRoute(points, settings, state, glm::vec3(5.0f, 0.0f, 40.0f), 0.1f, true);
        CHECK(step.action == bt::RouteAction::IssueDestination);
        CHECK(step.targetIndex == 0);
        CHECK_FALSE(state.dwelling);

        // Walking leg: abort mid-way, re-enter, same target.
        bt::RouteFollowerSettings direct;
        direct.startAtNearest = false;
        bt::RouteFollowerState walker;
        bt::stepRoute(points, direct, walker, points[0], 0.1f, true);
        step = bt::stepRoute(points, direct, walker, points[0], 0.1f, false); // -> marker 1
        REQUIRE(step.targetIndex == 1);
        step = bt::stepRoute(points, direct, walker, glm::vec3(5.0f, 0.0f, 0.0f), 0.1f, true);
        CHECK(step.action == bt::RouteAction::IssueDestination);
        CHECK(step.targetIndex == 1);
    }

    TEST_CASE("empty route fails")
    {
        const std::vector<glm::vec3> none;
        bt::RouteFollowerSettings settings;
        bt::RouteFollowerState state;
        auto step = bt::stepRoute(none, settings, state, glm::vec3(0.0f), 0.1f, true);
        CHECK(step.status == bt::BTNodeStatus::Failure);
        CHECK(step.action == bt::RouteAction::Stop);
        step = bt::stepRoute(none, settings, state, glm::vec3(0.0f), 0.1f, false);
        CHECK(step.status == bt::BTNodeStatus::Failure);
    }

    TEST_CASE("single point: Loop holds after arriving, Once succeeds")
    {
        const std::vector<glm::vec3> one = {glm::vec3(5.0f, 0.0f, 0.0f)};

        bt::RouteFollowerSettings loop;
        loop.mode = bt::RouteMode::Loop;
        bt::RouteFollowerState state;
        auto step = bt::stepRoute(one, loop, state, glm::vec3(0.0f), 0.1f, true);
        CHECK(step.action == bt::RouteAction::IssueDestination);
        step = bt::stepRoute(one, loop, state, one[0], 0.1f, false); // arrives
        CHECK(step.action == bt::RouteAction::Stop);
        CHECK(step.status == bt::BTNodeStatus::Running);
        for (int tick = 0; tick < 10; ++tick)
        {
            step = bt::stepRoute(one, loop, state, one[0], 0.1f, false);
            REQUIRE(step.action == bt::RouteAction::None);
            REQUIRE(step.status == bt::BTNodeStatus::Running);
        }

        bt::RouteFollowerSettings once;
        once.mode = bt::RouteMode::Once;
        bt::RouteFollowerState onceState;
        bt::stepRoute(one, once, onceState, glm::vec3(0.0f), 0.1f, true);
        step = bt::stepRoute(one, once, onceState, one[0], 0.1f, false);
        CHECK(step.status == bt::BTNodeStatus::Success);
        CHECK(step.action == bt::RouteAction::Stop);
    }

    TEST_CASE("a route that loses markers keeps a valid target")
    {
        auto points = line3();
        bt::RouteFollowerSettings settings;
        settings.startAtNearest = false;
        bt::RouteFollowerState state;
        bt::stepRoute(points, settings, state, points[0], 0.1f, true);
        bt::stepRoute(points, settings, state, points[0], 0.1f, false); // -> 1
        bt::stepRoute(points, settings, state, points[1], 0.1f, false); // -> 2
        REQUIRE(state.targetIndex == 2);

        points.pop_back(); // marker 2 deleted
        const auto step = bt::stepRoute(points, settings, state, glm::vec3(-50.0f, 0.0f, 0.0f), 0.1f, false);
        CHECK(step.targetIndex == 1);
        CHECK(step.status == bt::BTNodeStatus::Running);
    }
}
