#include <doctest.h>
#include <serialization/SceneSerialization.hpp>
#include <components/Components.hpp>
#include <navigation/RootMotionSpeed.hpp>
#include <nlohmann/json.hpp>

namespace
{
    using json = nlohmann::json;
}

TEST_SUITE("RootMotionSpeed")
{
    // EMA pacing helper converges toward the instantaneous root-motion speed
    // (planarDistance/dt) when that speed is under the cap.
    TEST_CASE("steady clip converges toward planar speed")
    {
        const float dist = 0.05f;
        const float dt = 0.016f;
        const float cap = 8.0f;
        const float expected = dist / dt; // ~3.125, comfortably under cap

        navigation::RootMotionSpeedParams params; // alpha=0.2, floorFraction=0.15
        float smoothed = 0.0f;
        float speed = 0.0f;

        for (int i = 0; i < 40; ++i)
        {
            speed = navigation::computeRootMotionCrowdSpeed(dist, dt, smoothed, cap, params);
            CHECK(speed <= cap + 1e-4f); // never exceeds the cap
        }

        // After ~40 EMA steps with alpha 0.2 the state is well within 1% of target.
        CHECK(smoothed == doctest::Approx(expected).epsilon(0.01));
        CHECK(speed == doctest::Approx(expected).epsilon(0.01));
    }

    // When instantaneous speed exceeds the cap, both the EMA state and the
    // returned speed clamp to the cap (inst is clamped before feeding the EMA).
    TEST_CASE("fast clip clamps to cap")
    {
        const float dist = 1.0f;
        const float dt = 0.016f; // dist/dt = 62.5, far above cap
        const float cap = 8.0f;

        navigation::RootMotionSpeedParams params;
        float smoothed = 0.0f;
        float speed = 0.0f;

        for (int i = 0; i < 60; ++i)
        {
            speed = navigation::computeRootMotionCrowdSpeed(dist, dt, smoothed, cap, params);
            CHECK(speed <= cap + 1e-4f);
        }

        CHECK(speed == doctest::Approx(cap).epsilon(0.001));
        CHECK(smoothed == doctest::Approx(cap).epsilon(0.001));
    }

    // A looping clip emits a single zero root-motion delta on each loop wrap.
    // The EMA must absorb that one zero frame as a small dip without collapsing
    // the crowd speed to a stall.
    TEST_CASE("single loop-wrap zero does not stall")
    {
        const float dist = 0.05f;
        const float dt = 0.016f;
        const float cap = 8.0f;

        navigation::RootMotionSpeedParams params;
        const float floor = params.floorFraction * cap; // 0.15 * 8 = 1.2

        float smoothed = 0.0f;

        // Warm up to steady speed.
        for (int i = 0; i < 40; ++i)
            navigation::computeRootMotionCrowdSpeed(dist, dt, smoothed, cap, params);

        const float steady = smoothed;

        // Inject a single zero-distance frame (loop wrap).
        float speedOnZero =
            navigation::computeRootMotionCrowdSpeed(0.0f, dt, smoothed, cap, params);

        // The one-frame zero is only a small EMA dip; it stays well above the floor.
        CHECK(speedOnZero >= floor);
        CHECK(speedOnZero > 0.5f * steady); // nowhere near a collapse to ~0

        // The stream recovers on the next normal frame.
        float speedAfter =
            navigation::computeRootMotionCrowdSpeed(dist, dt, smoothed, cap, params);
        CHECK(speedAfter >= floor);
        CHECK(speedAfter >= speedOnZero);
    }

    // With sustained zero distance the EMA state decays toward zero, but the
    // returned crowd speed is floored. This proves the floor lives in the return
    // value, not in the smoothed state.
    TEST_CASE("floor applies to return value not EMA state")
    {
        const float cap = 8.0f;
        navigation::RootMotionSpeedParams params;
        const float floor = params.floorFraction * cap; // 1.2

        float smoothed = 5.0f; // start above the floor

        float speed = 0.0f;
        for (int i = 0; i < 100; ++i)
            speed = navigation::computeRootMotionCrowdSpeed(0.0f, 0.016f, smoothed, cap, params);

        // Returned speed is still floored.
        CHECK(speed == doctest::Approx(floor));
        // ...but the underlying EMA state has decayed below the floor.
        CHECK(smoothed < floor);
        CHECK(smoothed >= 0.0f);
    }

    // Guard: a non-positive dt falls back to the current smoothed state for the
    // instantaneous reading (no divide-by-zero).
    TEST_CASE("zero dt falls back to smoothed state")
    {
        const float cap = 8.0f;
        navigation::RootMotionSpeedParams params;
        float smoothed = 3.0f;

        float speed = navigation::computeRootMotionCrowdSpeed(0.05f, 0.0f, smoothed, cap, params);

        // inst == smoothed, so the EMA stays put and the floored return is the state.
        CHECK(smoothed == doctest::Approx(3.0f));
        CHECK(speed == doctest::Approx(3.0f));
    }
}

TEST_SUITE("NavmeshAgentRootMotionSerialization")
{
    TEST_CASE("rootMotionDriven round-trips true")
    {
        components::NavmeshAgentComponent agent;
        agent.rootMotionDriven = true;

        json j = serialization::SceneSerialization::serializeNavmeshAgent(agent);
        REQUIRE(j.contains("rootMotionDriven"));
        CHECK(j["rootMotionDriven"].get<bool>() == true);

        components::NavmeshAgentComponent loaded;
        serialization::SceneSerialization::deserializeNavmeshAgent(j, loaded);
        CHECK(loaded.rootMotionDriven == true);
    }

    TEST_CASE("rootMotionDriven round-trips false")
    {
        components::NavmeshAgentComponent agent;
        agent.rootMotionDriven = false;

        json j = serialization::SceneSerialization::serializeNavmeshAgent(agent);
        CHECK(j["rootMotionDriven"].get<bool>() == false);

        components::NavmeshAgentComponent loaded;
        loaded.rootMotionDriven = true; // ensure deserialize actually writes it
        serialization::SceneSerialization::deserializeNavmeshAgent(j, loaded);
        CHECK(loaded.rootMotionDriven == false);
    }

    TEST_CASE("missing key keeps default false (back-compat)")
    {
        json empty = json::object();

        components::NavmeshAgentComponent loaded; // default is false
        serialization::SceneSerialization::deserializeNavmeshAgent(empty, loaded);
        CHECK(loaded.rootMotionDriven == false);
    }
}
