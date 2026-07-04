// CPU-only coverage for the VK-1471 per-emitter draw-order sort in VFXSortOrder.hpp:
//
//   * Emitters are ordered by ascending sortOrder.
//   * Equal sortOrder preserves the caller's input order (stable) — this is the
//     "ties keep current order" acceptance criterion and the "default 0 => identical
//     frame to today" guarantee.
//   * Negative sortOrder pushes an emitter behind the default-0 group.
//   * Empty / single / already-sorted inputs behave.
//
// Pure function of VFXSortOrder — no render/GPU types, no Vulkan device.

#include <doctest.h>

#include <vfx/VFXSortOrder.hpp>

#include <vector>

namespace
{
    // Extract the post-sort index sequence so ordering is easy to assert on.
    std::vector<uint32_t> indicesOf(const std::vector<vfx::VFXDrawOrderEntry>& entries)
    {
        std::vector<uint32_t> out;
        out.reserve(entries.size());
        for (const auto& e : entries)
        {
            out.push_back(e.index);
        }
        return out;
    }
}

TEST_SUITE("VFXSortOrder")
{
    TEST_CASE("emitters are ordered by ascending sortOrder")
    {
        // index carries the emitter identity; sortOrder is the key.
        std::vector<vfx::VFXDrawOrderEntry> entries = {
            {10u, 5}, {11u, 1}, {12u, 3}, {13u, 2}, {14u, 4}
        };
        vfx::stableSortDrawOrder(entries);
        CHECK(indicesOf(entries) == std::vector<uint32_t>{11u, 13u, 12u, 14u, 10u});
    }

    TEST_CASE("equal sortOrder preserves input order (stable tie-break)")
    {
        // Every entry shares the same key: the sort must be a no-op on order.
        std::vector<vfx::VFXDrawOrderEntry> entries = {
            {0u, 7}, {1u, 7}, {2u, 7}, {3u, 7}
        };
        vfx::stableSortDrawOrder(entries);
        CHECK(indicesOf(entries) == std::vector<uint32_t>{0u, 1u, 2u, 3u});
    }

    TEST_CASE("all-default (zero) sortOrder leaves the draw order identical")
    {
        // The "default 0 -> identical frame to today" acceptance criterion: build in
        // the pipeline's current iteration order, sort, and get exactly that back.
        std::vector<vfx::VFXDrawOrderEntry> entries;
        for (uint32_t i = 0; i < 8; ++i)
        {
            entries.push_back({i, 0});
        }
        vfx::stableSortDrawOrder(entries);
        CHECK(indicesOf(entries) == std::vector<uint32_t>{0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u});
    }

    TEST_CASE("ties within a group keep relative order while groups reorder")
    {
        // Two emitters share key 0 and two share key -1; the -1 group moves ahead but
        // each group's internal order is preserved.
        std::vector<vfx::VFXDrawOrderEntry> entries = {
            {100u, 0}, {200u, -1}, {300u, 0}, {400u, -1}
        };
        vfx::stableSortDrawOrder(entries);
        CHECK(indicesOf(entries) == std::vector<uint32_t>{200u, 400u, 100u, 300u});
    }

    TEST_CASE("negative sortOrder is drawn behind the default group")
    {
        std::vector<vfx::VFXDrawOrderEntry> entries = {
            {1u, 0}, {2u, -5}, {3u, 5}
        };
        vfx::stableSortDrawOrder(entries);
        CHECK(indicesOf(entries) == std::vector<uint32_t>{2u, 1u, 3u});
        // Lowest key first == submitted first == drawn behind.
        CHECK(entries.front().sortOrder == -5);
        CHECK(entries.back().sortOrder == 5);
    }

    TEST_CASE("already-sorted input is unchanged")
    {
        std::vector<vfx::VFXDrawOrderEntry> entries = {
            {0u, -2}, {1u, 0}, {2u, 1}, {3u, 9}
        };
        vfx::stableSortDrawOrder(entries);
        CHECK(indicesOf(entries) == std::vector<uint32_t>{0u, 1u, 2u, 3u});
    }

    TEST_CASE("empty and single-element inputs are handled")
    {
        std::vector<vfx::VFXDrawOrderEntry> empty;
        vfx::stableSortDrawOrder(empty);
        CHECK(empty.empty());

        std::vector<vfx::VFXDrawOrderEntry> single = {{42u, 3}};
        vfx::stableSortDrawOrder(single);
        REQUIRE(single.size() == 1);
        CHECK(single[0].index == 42u);
    }

    TEST_CASE("drawOrderLess is a strict-weak comparator on the key")
    {
        vfx::VFXDrawOrderEntry a{0u, 1};
        vfx::VFXDrawOrderEntry b{1u, 2};
        CHECK(vfx::drawOrderLess(a, b));
        CHECK_FALSE(vfx::drawOrderLess(b, a));
        vfx::VFXDrawOrderEntry c{2u, 1};
        CHECK_FALSE(vfx::drawOrderLess(a, c)); // equal keys => not-less both ways
        CHECK_FALSE(vfx::drawOrderLess(c, a));
    }
}
