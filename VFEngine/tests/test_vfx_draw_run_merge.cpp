// CPU-only coverage for the VK-1481 Phase 2 VFX draw-call merge condition in VFXDrawRunMerge.hpp.
// This is the consecutive-run batching logic extracted out of the scene/ribbon/distortion GPU render
// paths so it can be exercised without a Vulkan device.
//
// Pure function of VFXDrawRunMerge — no render/GPU types, no Vulkan device.

#include <doctest.h>

#include <vfx/VFXDrawRunMerge.hpp>

#include <cstdint>
#include <vector>

namespace
{
    struct Run
    {
        uint32_t baseSlot;
        uint32_t runLen;
        uint32_t key;
    };

    // Collect keyed runs into a vector for assertions.
    std::vector<Run> collectKeyed(const std::vector<uint32_t>& slots, const std::vector<uint32_t>& keys)
    {
        std::vector<Run> runs;
        vfx::forEachDrawRun(slots, keys, [&](uint32_t base, uint32_t len, uint32_t key) {
            runs.push_back({base, len, key});
        });
        return runs;
    }

    // Collect key-less runs (key recorded as 0).
    std::vector<Run> collectNoKey(const std::vector<uint32_t>& slots)
    {
        std::vector<Run> runs;
        vfx::forEachDrawRun(slots, [&](uint32_t base, uint32_t len) {
            runs.push_back({base, len, 0u});
        });
        return runs;
    }
}

TEST_SUITE("VFXDrawRunMerge")
{
    TEST_CASE("empty input yields no runs")
    {
        CHECK(collectKeyed({}, {}).empty());
        CHECK(collectNoKey({}).empty());
    }

    TEST_CASE("a single drawable is one run of length 1")
    {
        auto runs = collectKeyed({5u}, {1u});
        REQUIRE(runs.size() == 1u);
        CHECK(runs[0].baseSlot == 5u);
        CHECK(runs[0].runLen == 1u);
        CHECK(runs[0].key == 1u);
    }

    TEST_CASE("consecutive slots with the same key merge into one run")
    {
        auto runs = collectKeyed({3u, 4u, 5u, 6u}, {0u, 0u, 0u, 0u});
        REQUIRE(runs.size() == 1u);
        CHECK(runs[0].baseSlot == 3u);
        CHECK(runs[0].runLen == 4u);
        CHECK(runs[0].key == 0u);
    }

    TEST_CASE("a slot gap breaks the run")
    {
        // 3,4 consecutive; 6,7 consecutive; gap at 5 splits them.
        auto runs = collectKeyed({3u, 4u, 6u, 7u}, {0u, 0u, 0u, 0u});
        REQUIRE(runs.size() == 2u);
        CHECK(runs[0].baseSlot == 3u);
        CHECK(runs[0].runLen == 2u);
        CHECK(runs[1].baseSlot == 6u);
        CHECK(runs[1].runLen == 2u);
    }

    TEST_CASE("a key change breaks the run even when slots are consecutive")
    {
        // Slots 10,11,12 are consecutive, but the middle one is a different pipeline key.
        auto runs = collectKeyed({10u, 11u, 12u}, {0u, 1u, 1u});
        REQUIRE(runs.size() == 2u);
        CHECK(runs[0].baseSlot == 10u);
        CHECK(runs[0].runLen == 1u);
        CHECK(runs[0].key == 0u);
        CHECK(runs[1].baseSlot == 11u);
        CHECK(runs[1].runLen == 2u);
        CHECK(runs[1].key == 1u);
    }

    TEST_CASE("key-less overload merges purely on slot consecutiveness")
    {
        auto merged = collectNoKey({0u, 1u, 2u});
        REQUIRE(merged.size() == 1u);
        CHECK(merged[0].baseSlot == 0u);
        CHECK(merged[0].runLen == 3u);

        auto split = collectNoKey({0u, 1u, 3u, 4u, 9u});
        REQUIRE(split.size() == 3u);
        CHECK(split[0].baseSlot == 0u);
        CHECK(split[0].runLen == 2u);
        CHECK(split[1].baseSlot == 3u);
        CHECK(split[1].runLen == 2u);
        CHECK(split[2].baseSlot == 9u);
        CHECK(split[2].runLen == 1u);
    }
}
