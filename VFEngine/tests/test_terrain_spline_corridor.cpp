#include <doctest.h>

#include <terrain/SegmentCorridor.hpp>
#include <terrain/SplineSampling.hpp>

#include <array>
#include <limits>
#include <vector>

namespace
{
    terrain::SegmentProjection originalProjection(
        const glm::vec2& point,
        const glm::vec2& a,
        const glm::vec2& b,
        float segLength)
    {
        glm::vec2 segDir = b - a;
        glm::vec2 segNorm = segDir / segLength;
        float t = glm::dot(point - a, segNorm) / segLength;
        t = glm::clamp(t, 0.0f, 1.0f);

        glm::vec2 closest = a + segDir * t;
        float distance = glm::length(point - closest);
        return {t, distance};
    }

    float originalCorridorBlend(float distance, float halfWidth, float falloffWidth)
    {
        float blend = 1.0f;
        if (distance > halfWidth && falloffWidth > 0.0f)
        {
            float falloffT = (distance - halfWidth) / falloffWidth;
            blend = 1.0f - falloffT * falloffT * (3.0f - 2.0f * falloffT);
        }
        return blend;
    }

    void checkVec3(const glm::vec3& actual, const glm::vec3& expected)
    {
        CHECK(actual.x == expected.x);
        CHECK(actual.y == expected.y);
        CHECK(actual.z == expected.z);
    }
}

TEST_SUITE("TerrainSplineCorridor")
{
    TEST_CASE("segment projection clamps to both endpoints and preserves interior t")
    {
        const glm::vec2 a(2.0f, 3.0f);
        const glm::vec2 b(6.0f, 3.0f);
        constexpr float length = 4.0f;

        const auto before = terrain::projectOntoSegment(glm::vec2(0.0f, 6.0f), a, b, length);
        CHECK(before.t == 0.0f);
        CHECK(before.distance == doctest::Approx(glm::length(glm::vec2(0.0f, 6.0f) - a)));

        const auto inside = terrain::projectOntoSegment(glm::vec2(3.0f, 5.0f), a, b, length);
        CHECK(inside.t == 0.25f);
        CHECK(inside.distance == 2.0f);

        const auto after = terrain::projectOntoSegment(glm::vec2(9.0f, 7.0f), a, b, length);
        CHECK(after.t == 1.0f);
        CHECK(after.distance == 5.0f);
    }

    TEST_CASE("segment projection is exactly equivalent to the original arithmetic")
    {
        const glm::vec2 a(-3.0f, 2.0f);
        const glm::vec2 b(5.0f, 8.0f);
        const float length = glm::length(b - a);
        const std::array<glm::vec2, 5> points = {
            glm::vec2(-10.0f, -4.0f),
            glm::vec2(-3.0f, 2.0f),
            glm::vec2(0.0f, 7.0f),
            glm::vec2(5.0f, 8.0f),
            glm::vec2(12.0f, 20.0f)
        };

        for (const glm::vec2& point : points)
        {
            const auto expected = originalProjection(point, a, b, length);
            const auto actual = terrain::projectOntoSegment(point, a, b, length);
            CHECK(actual.t == expected.t);
            CHECK(actual.distance == expected.distance);
        }
    }

    TEST_CASE("corridor blend is exactly equivalent to the original arithmetic")
    {
        constexpr std::array<float, 6> distances = {0.0f, 2.0f, 3.0f, 5.0f, 7.0f, 9.0f};
        constexpr std::array<float, 3> halfWidths = {0.0f, 2.0f, 5.0f};
        constexpr std::array<float, 4> falloffs = {-1.0f, 0.0f, 2.0f, 4.0f};

        for (float distance : distances)
        {
            for (float halfWidth : halfWidths)
            {
                for (float falloff : falloffs)
                {
                    CHECK(terrain::corridorBlend(distance, halfWidth, falloff)
                          == originalCorridorBlend(distance, halfWidth, falloff));
                }
            }
        }
    }

    TEST_CASE("corridor blend retains full influence for a NaN distance")
    {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        CHECK(terrain::corridorBlend(nan, 2.0f, 3.0f) == 1.0f);
    }

    TEST_CASE("Catmull-Rom evaluation interpolates segment endpoints")
    {
        const glm::vec3 p0(-1.0f, 2.0f, 0.0f);
        const glm::vec3 p1(0.0f, 3.0f, 1.0f);
        const glm::vec3 p2(2.0f, 5.0f, 4.0f);
        const glm::vec3 p3(7.0f, 11.0f, 6.0f);

        checkVec3(terrain::evaluateCatmullRom(p0, p1, p2, p3, 0.0f), p1);
        checkVec3(terrain::evaluateCatmullRom(p0, p1, p2, p3, 1.0f), p2);
    }

    TEST_CASE("spline sampling handles zero and one control point")
    {
        CHECK(terrain::sampleSplineCurve({}, 1.0f).empty());

        const std::vector<terrain::SplineControlPoint> one = {{{1.0f, 2.0f, 3.0f}}};
        CHECK(terrain::sampleSplineCurve(one, 1.0f).empty());
    }

    TEST_CASE("two-point spline sampling is linear and includes both endpoints")
    {
        const std::vector<terrain::SplineControlPoint> points = {
            {{0.0f, 0.0f, 0.0f}},
            {{4.0f, 2.0f, 0.0f}}
        };

        const auto samples = terrain::sampleSplineCurve(points, glm::length(points[1].position) * 0.5f);
        REQUIRE(samples.size() == 3);
        checkVec3(samples.front(), points.front().position);
        checkVec3(samples[1], glm::vec3(2.0f, 1.0f, 0.0f));
        checkVec3(samples.back(), points.back().position);
    }

    TEST_CASE("Catmull-Rom sampling covers every segment and appends the final point once")
    {
        const std::vector<terrain::SplineControlPoint> points = {
            {{0.0f, 0.0f, 0.0f}},
            {{2.0f, 0.0f, 0.0f}},
            {{4.0f, 0.0f, 0.0f}},
            {{6.0f, 0.0f, 0.0f}}
        };

        const auto samples = terrain::sampleSplineCurve(points, 1.0f);
        REQUIRE(samples.size() == 7);
        checkVec3(samples[0], points[0].position);
        checkVec3(samples[2], points[1].position);
        checkVec3(samples[4], points[2].position);
        checkVec3(samples[6], points[3].position);
    }
}
