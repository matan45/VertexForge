// VK-1653 — the stroke math the MCP terrain tools stand on (terrain/BrushStroke.hpp).
//
// Pure, header-only arithmetic: no TerrainService, no grid, no dispatcher. The overlap values are
// worked by hand at spacing r/4, where every distance is a multiple of 1/8 and therefore exact in
// float -- so where the falloff is a polynomial with dyadic coefficients the expected sums are exact
// too, and a tolerance only guards against summation order.

#include <doctest.h>

#include "terrain/BrushStroke.hpp"

#include <glm/glm.hpp>

#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace
{
    using terrain::BrushFalloff;
    using terrain::BrushShape;

    // True when `p` lies on the segment [a, b], within a tolerance for the float interpolation.
    bool onSegment(const glm::vec2& p, const glm::vec2& a, const glm::vec2& b)
    {
        const glm::vec2 ab = b - a;
        const float lengthSq = glm::dot(ab, ab);
        if (lengthSq <= 0.0f)
            return glm::length(p - a) < 1e-4f;
        const float t = glm::clamp(glm::dot(p - a, ab) / lengthSq, 0.0f, 1.0f);
        return glm::length(a + ab * t - p) < 1e-4f;
    }
}

TEST_SUITE("TerrainBrushStroke")
{
    TEST_CASE("brush_stroke: centreline overlap at a quarter-radius spacing matches the hand-worked sums")
    {
        // Dabs at k*s for |k|*s < r, s = r/4, so k in [-3, 3]; the k = +-4 dabs sit exactly on the
        // exclusive rim and contribute nothing.
        //   Constant: 1 + 2*(1 + 1 + 1)                    = 7
        //   Linear:   1 + 2*(0.75 + 0.5 + 0.25)            = 4
        //   Smooth:   1 + 2*(0.84375 + 0.5 + 0.15625)      = 4
        //   Sharp:    1 + 2*(0.9375 + 0.75 + 0.4375)       = 5.25
        CHECK(terrain::centrelineOverlap(1.0f, 4.0f, BrushFalloff::Constant) == doctest::Approx(7.0));
        CHECK(terrain::centrelineOverlap(1.0f, 4.0f, BrushFalloff::Linear) == doctest::Approx(4.0));
        CHECK(terrain::centrelineOverlap(1.0f, 4.0f, BrushFalloff::Smooth) == doctest::Approx(4.0));
        CHECK(terrain::centrelineOverlap(1.0f, 4.0f, BrushFalloff::Sharp) == doctest::Approx(5.25));

        // The value depends on the RATIO only.
        CHECK(terrain::centrelineOverlap(2.5f, 10.0f, BrushFalloff::Sharp) == doctest::Approx(5.25));
    }

    TEST_CASE("brush_stroke: Linear and Smooth are a partition of unity at r/4 and r/2")
    {
        // f(t) + f(1 - t) = 1 for both curves, so dabs every r/N sum to exactly N. That is what lets
        // a straight stroke raise its whole interior centreline by the same `amount`.
        for (BrushFalloff falloff : {BrushFalloff::Linear, BrushFalloff::Smooth})
        {
            CAPTURE(static_cast<int>(falloff));
            CHECK(terrain::centrelineOverlap(1.0f, 4.0f, falloff) == doctest::Approx(4.0));
            CHECK(terrain::centrelineOverlap(2.0f, 4.0f, falloff) == doctest::Approx(2.0));
        }
    }

    TEST_CASE("brush_stroke: degenerate overlap inputs fall back to a single dab")
    {
        CHECK(terrain::centrelineOverlap(0.0f, 4.0f, BrushFalloff::Linear) == 1.0f);
        CHECK(terrain::centrelineOverlap(1.0f, 0.0f, BrushFalloff::Linear) == 1.0f);
        // Spacing at or past the radius: only the dab itself is inside the rim.
        CHECK(terrain::centrelineOverlap(4.0f, 4.0f, BrushFalloff::Constant) == 1.0f);
    }

    TEST_CASE("brush_stroke: a lone dab is its own peak")
    {
        const std::vector<glm::vec2> one{glm::vec2(3.0f, -7.0f)};
        for (BrushFalloff falloff : {BrushFalloff::Constant, BrushFalloff::Linear,
                                     BrushFalloff::Smooth, BrushFalloff::Sharp})
        {
            CAPTURE(static_cast<int>(falloff));
            CHECK(terrain::strokePeakOverlap(one, 5.0f, falloff, BrushShape::Circle) == 1.0f);
            CHECK(terrain::strokePeakOverlap(one, 5.0f, falloff, BrushShape::Square) == 1.0f);
        }

        CHECK(terrain::strokePeakOverlap({}, 5.0f, BrushFalloff::Linear, BrushShape::Circle) == 1.0f);
    }

    TEST_CASE("brush_stroke: the peak of a long straight stroke equals the infinite-line overlap")
    {
        // 64 m at radius 4 and spacing 0.25 -> dabs every metre, i.e. s = r/4. A power-of-two length
        // keeps every interpolation parameter i/64 exact, so the dabs sit on exact integers and the
        // rim dabs (exactly 4 m away) are excluded rather than included by a rounding error.
        const std::vector<glm::vec2> line{glm::vec2(0.0f, 0.0f), glm::vec2(64.0f, 0.0f)};
        const std::vector<glm::vec2> dabs = terrain::resamplePolyline(line, 0.25f * 4.0f);
        REQUIRE(dabs.size() == 65);
        CHECK(dabs[17] == glm::vec2(17.0f, 0.0f));

        for (BrushFalloff falloff : {BrushFalloff::Linear, BrushFalloff::Smooth})
        {
            CAPTURE(static_cast<int>(falloff));
            const float expected = terrain::centrelineOverlap(1.0f, 4.0f, falloff);
            CHECK(terrain::strokePeakOverlap(dabs, 4.0f, falloff, BrushShape::Circle)
                  == doctest::Approx(expected).epsilon(1e-5));
        }

        // Constant is NOT a partition of unity: midway between dabs all eight neighbours are inside
        // the rim, so the peak (8) exceeds the at-dab sum (7). The stroke code divides by the peak.
        CHECK(terrain::strokePeakOverlap(dabs, 4.0f, BrushFalloff::Constant, BrushShape::Circle)
              == doctest::Approx(8.0));
    }

    TEST_CASE("brush_stroke: resampling an empty or zero-length polyline")
    {
        CHECK(terrain::resamplePolyline({}, 1.0f).empty());

        float spacing = -1.0f;
        const std::vector<glm::vec2> single =
            terrain::resamplePolyline({glm::vec2(2.0f, 3.0f)}, 1.0f, &spacing);
        REQUIRE(single.size() == 1);
        CHECK(single[0] == glm::vec2(2.0f, 3.0f));
        CHECK(spacing == 0.0f);

        // Every point coincident: still exactly one dab, at the shared point.
        const std::vector<glm::vec2> coincident = terrain::resamplePolyline(
            {glm::vec2(1.0f, 2.0f), glm::vec2(1.0f, 2.0f), glm::vec2(1.0f, 2.0f)}, 0.5f);
        REQUIRE(coincident.size() == 1);
        CHECK(coincident[0] == glm::vec2(1.0f, 2.0f));
    }

    TEST_CASE("brush_stroke: resampling places uniform dabs on the polyline with exact endpoints")
    {
        // An L: 10 m along X, then 7 m along Z. L = 17, max spacing 2 -> ceil(8.5) = 9 segments.
        const std::vector<glm::vec2> points{glm::vec2(0.0f, 0.0f), glm::vec2(10.0f, 0.0f),
                                            glm::vec2(10.0f, 7.0f)};
        float spacing = 0.0f;
        const std::vector<glm::vec2> dabs = terrain::resamplePolyline(points, 2.0f, &spacing);

        REQUIRE(dabs.size() == 10);
        CHECK(spacing == doctest::Approx(17.0 / 9.0));
        CHECK(spacing <= 2.0f);

        CHECK(dabs.front() == points.front());
        CHECK(dabs.back() == points.back()); // exact, not interpolated

        for (size_t i = 0; i < dabs.size(); ++i)
        {
            CAPTURE(i);
            CHECK((onSegment(dabs[i], points[0], points[1]) || onSegment(dabs[i], points[1], points[2])));
            if (i > 0)
            {
                // Euclidean <= arc length, which is the uniform step (shorter across the corner).
                CHECK(glm::length(dabs[i] - dabs[i - 1]) <= spacing + 1e-4f);
            }
        }

        // Along the first leg the dabs are exactly one step apart in arc length.
        CHECK(dabs[1].x == doctest::Approx(17.0 / 9.0).epsilon(1e-5));
        CHECK(dabs[1].y == 0.0f);
    }

    TEST_CASE("brush_stroke: duplicate interior points do not disturb the resampling")
    {
        const std::vector<glm::vec2> withDuplicate{glm::vec2(0.0f, 0.0f), glm::vec2(5.0f, 0.0f),
                                                   glm::vec2(5.0f, 0.0f), glm::vec2(10.0f, 0.0f)};
        const std::vector<glm::vec2> dabs = terrain::resamplePolyline(withDuplicate, 2.5f);

        REQUIRE(dabs.size() == 5);
        const float expectedX[] = {0.0f, 2.5f, 5.0f, 7.5f, 10.0f};
        for (size_t i = 0; i < dabs.size(); ++i)
        {
            CAPTURE(i);
            CHECK(dabs[i].x == doctest::Approx(expectedX[i]).epsilon(1e-6));
            CHECK(dabs[i].y == 0.0f);
        }
    }

    TEST_CASE("brush_stroke: resampledDabCount agrees with resamplePolyline and saturates")
    {
        CHECK(terrain::resampledDabCount(0.0f, 1.0f) == 1);
        CHECK(terrain::resampledDabCount(10.0f, 0.0f) == 1);
        CHECK(terrain::resampledDabCount(std::numeric_limits<float>::quiet_NaN(), 1.0f) == 1);
        CHECK(terrain::resampledDabCount(10.0f, 2.5f) == 5); // exact multiple: 4 segments
        CHECK(terrain::resampledDabCount(10.0f, 3.0f) == 5); // ceil(3.33) = 4 segments

        const std::vector<glm::vec2> line{glm::vec2(-3.0f, 1.0f), glm::vec2(4.0f, 9.0f)};
        const float length = terrain::polylineLength(line);
        CHECK(terrain::resamplePolyline(line, 0.7f).size() == terrain::resampledDabCount(length, 0.7f));

        // Absurd requests saturate instead of overflowing, so a caller can refuse before allocating.
        CHECK(terrain::resampledDabCount(1.0e30f, 1.0e-6f) == std::numeric_limits<uint64_t>::max());
        CHECK(terrain::resampledDabCount(std::numeric_limits<float>::infinity(), 1.0f)
              == std::numeric_limits<uint64_t>::max());
    }

    TEST_CASE("brush_stroke: square brushes measure Chebyshev distance")
    {
        const glm::vec2 centre(0.0f, 0.0f);
        CHECK(terrain::brushNormalizedDistance(glm::vec2(3.0f, 4.0f), centre, 5.0f, BrushShape::Circle)
              == doctest::Approx(1.0));
        CHECK(terrain::brushNormalizedDistance(glm::vec2(3.0f, 4.0f), centre, 5.0f, BrushShape::Square)
              == doctest::Approx(0.8));
    }
}
