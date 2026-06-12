#include <doctest.h>
#include <stats/FrameHistoryMath.hpp>
#include <vector>

// ============================================================
// FrameHistoryMath: median + hitch detection backing the
// profiler UI's CPU/GPU frame-history plots.
// ============================================================

TEST_SUITE("FrameHistoryMath")
{

TEST_CASE("median of empty, odd and even sized inputs")
{
    CHECK(render::history::median({}) == 0.0f);
    CHECK(render::history::median({5.0f}) == 5.0f);
    CHECK(render::history::median({3.0f, 1.0f, 2.0f}) == 2.0f);
    CHECK(render::history::median({4.0f, 1.0f, 3.0f, 2.0f}) == 2.5f);
    // Unsorted input with duplicates
    CHECK(render::history::median({10.0f, 10.0f, 1.0f, 10.0f, 10.0f}) == 10.0f);
}

TEST_CASE("findHitches flags frames above factor x median")
{
    // Steady 8ms frames with two spikes
    std::vector<float> frames(20, 8.0f);
    frames[5] = 40.0f;
    frames[12] = 17.0f;

    auto hitches = render::history::findHitches(frames);
    REQUIRE(hitches.size() == 2);
    CHECK(hitches[0] == 5);
    CHECK(hitches[1] == 12);
}

TEST_CASE("findHitches needs a minimum sample count")
{
    CHECK(render::history::findHitches({1.0f, 100.0f}).empty());
    CHECK(render::history::findHitches({1.0f, 1.0f, 100.0f}).empty());
}

TEST_CASE("findHitches ignores sub-millisecond noise via the floor")
{
    // 0.2ms frames doubling to 0.5ms is noise, not a hitch
    std::vector<float> frames(20, 0.2f);
    frames[3] = 0.5f;
    CHECK(render::history::findHitches(frames).empty());

    // But a real spike above the 1ms floor still registers
    frames[7] = 5.0f;
    auto hitches = render::history::findHitches(frames);
    REQUIRE(hitches.size() == 1);
    CHECK(hitches[0] == 7);
}

TEST_CASE("steady frame times produce no hitches")
{
    std::vector<float> frames(120, 16.6f);
    CHECK(render::history::findHitches(frames).empty());
}

}
