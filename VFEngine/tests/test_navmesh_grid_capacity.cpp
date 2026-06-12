#include <doctest.h>
#include <navigation/NavmeshData.hpp>

// ============================================================
// dtPolyRef bit-budget math: maxTiles passed to dtNavMesh::init
// caps RESIDENT tiles and must leave saltBits >= 10 (Detour
// fails init below that). Open-world grids get clamped.
// ============================================================

TEST_SUITE("NavmeshGridCapacity")
{
    TEST_CASE("open-world grid is clamped to the resident budget")
    {
        // ±10000 unit bounds at 32-unit tiles ≈ 626x626 ≈ 391k grid tiles —
        // unclamped this would leave saltBits < 10 and dtNavMesh::init fails
        auto budget = navigation::computeNavmeshRefBudget(391876, 2048);

        CHECK(budget.clamped);
        CHECK(budget.maxTiles == 1024);
        CHECK(budget.polyBits == 11);
        CHECK(budget.tileBits + budget.polyBits + budget.saltBits == 32);
        CHECK(budget.saltBits >= 10);
    }

    TEST_CASE("small grids pass through unclamped")
    {
        auto budget = navigation::computeNavmeshRefBudget(100, 2048);

        CHECK_FALSE(budget.clamped);
        CHECK(budget.maxTiles == 100);
        CHECK(budget.saltBits >= 10);
        CHECK(budget.tileBits + budget.polyBits + budget.saltBits == 32);
    }

    TEST_CASE("exactly at the cap is not clamped")
    {
        auto budget = navigation::computeNavmeshRefBudget(1024, 2048);
        CHECK_FALSE(budget.clamped);
        CHECK(budget.maxTiles == 1024);
        CHECK(budget.saltBits >= 10);
    }

    TEST_CASE("degenerate inputs")
    {
        auto zero = navigation::computeNavmeshRefBudget(0, 2048);
        CHECK(zero.maxTiles == 1);
        CHECK(zero.saltBits >= 10);

        auto negative = navigation::computeNavmeshRefBudget(-5, 2048);
        CHECK(negative.maxTiles == 1);
    }

    TEST_CASE("larger poly budgets shrink the resident tile cap")
    {
        // 2^15 polys per tile leaves 32-10-15 = 7 tile bits -> 128 tiles max
        auto budget = navigation::computeNavmeshRefBudget(391876, 1 << 15);

        CHECK(budget.polyBits == 15);
        CHECK(budget.maxTiles == 128);
        CHECK(budget.saltBits >= 10);
        CHECK(budget.clamped);
    }

    TEST_CASE("bit helper rounds up to the next power of two")
    {
        CHECK(navigation::navmeshBitsFor(2) == 1);
        CHECK(navigation::navmeshBitsFor(3) == 2);
        CHECK(navigation::navmeshBitsFor(1024) == 10);
        CHECK(navigation::navmeshBitsFor(1025) == 11);
        CHECK(navigation::navmeshBitsFor(2048) == 11);
    }
}
