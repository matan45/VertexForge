#include <doctest.h>
#include <render/FlipbookMath.hpp>
#include <glm/glm.hpp>
#include <cmath>

// ============================================================
// Phase 1: Billboard flipbook / animation math
// Mirrors resources/shaders/billboard/billboard.glsl so the CPU
// gather path and the GPU shader agree on frame selection.
// ============================================================

using render::computeFlipbookFrame;
using render::flipbookFinished;
using render::pulseScale;
using render::spinAngle;

TEST_CASE("FlipbookMath: no animation returns full UV rect")
{
    SUBCASE("single frame (1x1)")
    {
        auto f = computeFlipbookFrame(3.7f, 12.0f, 1, 1);
        CHECK(f.uvOffset.x == doctest::Approx(0.0f));
        CHECK(f.uvOffset.y == doctest::Approx(0.0f));
        CHECK(f.uvScale.x == doctest::Approx(1.0f));
        CHECK(f.uvScale.y == doctest::Approx(1.0f));
    }

    SUBCASE("zero frame rate")
    {
        auto f = computeFlipbookFrame(3.7f, 0.0f, 4, 4);
        CHECK(f.uvOffset.x == doctest::Approx(0.0f));
        CHECK(f.uvOffset.y == doctest::Approx(0.0f));
        CHECK(f.uvScale.x == doctest::Approx(1.0f));
        CHECK(f.uvScale.y == doctest::Approx(1.0f));
    }

    SUBCASE("negative frame rate")
    {
        auto f = computeFlipbookFrame(1.0f, -5.0f, 4, 4);
        CHECK(f.uvScale.x == doctest::Approx(1.0f));
        CHECK(f.uvScale.y == doctest::Approx(1.0f));
    }
}

TEST_CASE("FlipbookMath: frame selection matches GLSL formula")
{
    const int cols = 4;
    const int rows = 2; // 8 frames total
    const float frameRate = 10.0f;

    // At t=0 -> frame 0 -> col 0, row 0
    {
        auto f = computeFlipbookFrame(0.0f, frameRate, cols, rows);
        CHECK(f.uvOffset.x == doctest::Approx(0.0f));
        CHECK(f.uvOffset.y == doctest::Approx(0.0f));
        CHECK(f.uvScale.x == doctest::Approx(0.25f)); // 1/cols
        CHECK(f.uvScale.y == doctest::Approx(0.5f));  // 1/rows
    }

    // t=0.25s, 10fps -> frame 2.5 -> floor 2 -> col 2, row 0
    {
        auto f = computeFlipbookFrame(0.25f, frameRate, cols, rows);
        CHECK(f.uvOffset.x == doctest::Approx(0.5f));  // col 2 * 0.25
        CHECK(f.uvOffset.y == doctest::Approx(0.0f));
    }

    // t=0.5s -> frame 5 -> col mod(5,4)=1, row floor(5/4)=1
    {
        auto f = computeFlipbookFrame(0.5f, frameRate, cols, rows);
        CHECK(f.uvOffset.x == doctest::Approx(0.25f)); // col 1 * 0.25
        CHECK(f.uvOffset.y == doctest::Approx(0.5f));  // row 1 * 0.5
    }

    // Wraps: t=0.8s -> frame 8 -> mod(8,8)=0 -> col 0, row 0
    {
        auto f = computeFlipbookFrame(0.8f, frameRate, cols, rows);
        CHECK(f.uvOffset.x == doctest::Approx(0.0f));
        CHECK(f.uvOffset.y == doctest::Approx(0.0f));
    }
}

TEST_CASE("FlipbookMath: play-once clamps to the last frame and holds")
{
    const int cols = 4;
    const int rows = 2; // 8 frames total
    const float frameRate = 10.0f;
    const float tileX = 0.25f; // 1/cols
    const float tileY = 0.5f;  // 1/rows

    // Mid-animation it tracks the loop path: t=0.25s -> frame 2 -> col 2, row 0.
    {
        auto f = computeFlipbookFrame(0.25f, frameRate, cols, rows, false);
        CHECK(f.uvOffset.x == doctest::Approx(0.5f)); // col 2 * 0.25
        CHECK(f.uvOffset.y == doctest::Approx(0.0f));
        CHECK(f.uvScale.x == doctest::Approx(tileX));
        CHECK(f.uvScale.y == doctest::Approx(tileY));
    }

    // At one full cycle (t=0.8s -> frame 8) it holds the LAST tile (frame 7),
    // unlike loop which would wrap to frame 0.
    {
        auto once = computeFlipbookFrame(0.8f, frameRate, cols, rows, false);
        // frame 7 -> col mod(7,4)=3, row floor(7/4)=1
        CHECK(once.uvOffset.x == doctest::Approx(0.75f));
        CHECK(once.uvOffset.y == doctest::Approx(0.5f));

        auto loop = computeFlipbookFrame(0.8f, frameRate, cols, rows, true);
        CHECK(loop.uvOffset.x == doctest::Approx(0.0f)); // wraps to frame 0
        CHECK(loop.uvOffset.y == doctest::Approx(0.0f));
    }

    // Well past the end it stays on the last tile (holds, never advances).
    {
        auto f = computeFlipbookFrame(5.0f, frameRate, cols, rows, false);
        CHECK(f.uvOffset.x == doctest::Approx(0.75f)); // still frame 7
        CHECK(f.uvOffset.y == doctest::Approx(0.5f));
    }

    // The 4-arg overload still loops (backward compatible).
    {
        auto f4 = computeFlipbookFrame(0.8f, frameRate, cols, rows);
        CHECK(f4.uvOffset.x == doctest::Approx(0.0f));
        CHECK(f4.uvOffset.y == doctest::Approx(0.0f));
    }
}

TEST_CASE("FlipbookMath: flipbookFinished transitions false->true at one full cycle")
{
    const int cols = 4;
    const int rows = 2; // 8 frames; finishes when t*frameRate >= 8
    const float frameRate = 10.0f;

    // Looping animations never finish.
    CHECK_FALSE(flipbookFinished(0.79f, frameRate, cols, rows, true));
    CHECK_FALSE(flipbookFinished(100.0f, frameRate, cols, rows, true));

    // Non-animated (<=1 frame or rate<=0) never finishes either.
    CHECK_FALSE(flipbookFinished(100.0f, frameRate, 1, 1, false));
    CHECK_FALSE(flipbookFinished(100.0f, 0.0f, cols, rows, false));

    // One-shot: false just before the last frame completes, true at/after.
    CHECK_FALSE(flipbookFinished(0.79f, frameRate, cols, rows, false)); // 7.9 < 8
    CHECK(flipbookFinished(0.8f, frameRate, cols, rows, false));        // 8.0 >= 8
    CHECK(flipbookFinished(1.5f, frameRate, cols, rows, false));
}

TEST_CASE("FlipbookMath: pulse and spin defaults are identity")
{
    // amplitude 0 -> scale factor exactly 1
    CHECK(pulseScale(5.0f, 0.0f, 0.0f, 7.0f) == doctest::Approx(1.0f));
    // speed 0 -> angle exactly 0
    CHECK(spinAngle(5.0f, 0.0f, 0.0f) == doctest::Approx(0.0f));

    // animStartTime shifts the time origin
    CHECK(spinAngle(5.0f, 2.0f, 1.0f) == doctest::Approx(3.0f));
    CHECK(pulseScale(2.0f, 2.0f, 0.5f, 1.0f) == doctest::Approx(1.0f)); // sin(0)=0
}
