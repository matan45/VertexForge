#include <doctest.h>
#include "navigation/CorridorFollow.hpp"
#include <initializer_list>

namespace
{
    navigation::NavPath makePath(std::initializer_list<glm::vec3> points)
    {
        navigation::NavPath path;
        path.isValid = true;
        path.waypoints.assign(points.begin(), points.end());
        return path;
    }
}

TEST_SUITE("CorridorFollow")
{
    TEST_CASE("projection arclength is monotonic on a polyline")
    {
        auto path = makePath({{0.0f, 0.0f, 0.0f}, {10.0f, 0.0f, 0.0f}, {10.0f, 0.0f, 10.0f}});
        auto a = navigation::projectOntoCorridor(path, {2.0f, 0.0f, 1.0f});
        auto b = navigation::projectOntoCorridor(path, {10.0f, 0.0f, 5.0f});

        CHECK(a.segmentIndex == 0);
        CHECK(b.segmentIndex == 1);
        CHECK(a.arcLength < b.arcLength);
        CHECK(b.arcLength == doctest::Approx(15.0f));
    }

    TEST_CASE("point at arclength clamps to corridor ends")
    {
        auto path = makePath({{0.0f, 0.0f, 0.0f}, {10.0f, 0.0f, 0.0f}, {10.0f, 0.0f, 10.0f}});

        CHECK(navigation::corridorPointAtArcLength(path, -1.0f).x == doctest::Approx(0.0f));
        CHECK(navigation::corridorPointAtArcLength(path, 12.0f).z == doctest::Approx(2.0f));
        CHECK(navigation::corridorPointAtArcLength(path, 30.0f).z == doctest::Approx(10.0f));
    }

    TEST_CASE("follow velocity moves toward a lookahead carrot")
    {
        auto path = makePath({{0.0f, 0.0f, 0.0f}, {10.0f, 0.0f, 0.0f}});
        navigation::CorridorFollowParams params;
        params.lookAhead = 3.0f;
        params.arriveRadius = 1.0f;

        glm::vec3 v = navigation::corridorFollowVelocity(path, {1.0f, 0.0f, 1.0f}, 4.0f, params);
        CHECK(v.x > 0.0f);
        CHECK(v.z < 0.0f);
        CHECK(glm::length(v) <= 4.0001f);
    }

    TEST_CASE("lateral offset is clamped by corridor half width")
    {
        auto path = makePath({{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 10.0f}});
        navigation::CorridorFollowParams params;
        params.lookAhead = 4.0f;
        params.lateralOffset = 8.0f;
        params.corridorHalfWidth = 1.0f;

        glm::vec3 v = navigation::corridorFollowVelocity(path, {0.0f, 0.0f, 0.0f}, 2.0f, params);
        CHECK(v.x == doctest::Approx(0.485f).epsilon(0.05f));
        CHECK(v.z > 0.0f);
    }

    TEST_CASE("arrival and stale checks")
    {
        auto path = makePath({{0.0f, 0.0f, 0.0f}, {2.0f, 0.0f, 0.0f}});
        navigation::CorridorFollowParams params;
        params.arriveRadius = 1.5f;

        CHECK(glm::length(navigation::corridorFollowVelocity(path, {1.0f, 0.0f, 0.0f}, 3.0f, params)) == doctest::Approx(0.0f));
        CHECK_FALSE(navigation::corridorStale(7, 7));
        CHECK(navigation::corridorStale(7, 8));
    }
}
