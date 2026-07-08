#include <doctest.h>
#include <render/virtualtexture/VTDirtyChunks.hpp>

#include <vector>

// ============================================================================
// VT chunked page-table dirty tracking (VK-1480). Pure, CPU-only. Backs
// VTPageTable::uploadToGPU's "upload only the coalesced dirty ranges" path.
// CHUNK_ENTRIES = 4096 entries (16 KB) per chunk.
// ============================================================================

using namespace render::vt;

namespace
{
    constexpr uint32_t CHUNK = VTDirtyChunks::CHUNK_ENTRIES; // 4096

    std::vector<VTDirtyChunks::Range> take(VTDirtyChunks& dc)
    {
        std::vector<VTDirtyChunks::Range> out;
        dc.takeRanges(out);
        return out;
    }
}

TEST_CASE("VT dirty chunks: fresh tracker is clean")
{
    VTDirtyChunks dc;
    dc.reset(10000);
    CHECK(dc.any() == false);
    CHECK(take(dc).empty());
}

TEST_CASE("VT dirty chunks: markEntry dirties exactly its chunk")
{
    VTDirtyChunks dc;
    dc.reset(10000); // chunks 0:[0,4096) 1:[4096,8192) 2:[8192,10000)
    dc.markEntry(5000);
    CHECK(dc.any());

    const auto ranges = take(dc);
    REQUIRE(ranges.size() == 1u);
    CHECK(ranges[0].firstEntry == CHUNK);        // 4096
    CHECK(ranges[0].entryCount == CHUNK);        // whole chunk 1 (8192 < 10000)
}

TEST_CASE("VT dirty chunks: markRange within a chunk dirties the whole chunk")
{
    VTDirtyChunks dc;
    dc.reset(10000);
    dc.markRange(100, 50); // entries 100..149 all inside chunk 0

    const auto ranges = take(dc);
    REQUIRE(ranges.size() == 1u);
    CHECK(ranges[0].firstEntry == 0u);
    CHECK(ranges[0].entryCount == CHUNK);
}

TEST_CASE("VT dirty chunks: a range spanning a chunk boundary coalesces")
{
    VTDirtyChunks dc;
    dc.reset(10000);
    dc.markRange(4000, 200); // 4000..4199 -> chunk 0 and chunk 1

    const auto ranges = take(dc);
    REQUIRE(ranges.size() == 1u);
    CHECK(ranges[0].firstEntry == 0u);
    CHECK(ranges[0].entryCount == 2u * CHUNK); // [0, 8192)
}

TEST_CASE("VT dirty chunks: adjacent chunks coalesce, gaps split, tail clamps")
{
    VTDirtyChunks dc;
    dc.reset(20000); // chunks 0..4; chunk 4 = [16384, 20000)
    dc.markEntry(0);      // chunk 0
    dc.markEntry(CHUNK);  // chunk 1
    dc.markEntry(2 * CHUNK); // chunk 2
    // chunk 3 left clean -> a gap
    dc.markEntry(4 * CHUNK); // chunk 4 (the clamped tail)

    const auto ranges = take(dc);
    REQUIRE(ranges.size() == 2u);
    CHECK(ranges[0].firstEntry == 0u);
    CHECK(ranges[0].entryCount == 3u * CHUNK); // chunks 0,1,2 -> [0, 12288)
    CHECK(ranges[1].firstEntry == 4u * CHUNK); // 16384
    CHECK(ranges[1].entryCount == 20000u - 4u * CHUNK); // clamped to totalEntries
}

TEST_CASE("VT dirty chunks: markAll covers the whole table, clamped")
{
    VTDirtyChunks dc;
    dc.reset(5000); // chunks 0:[0,4096) 1:[4096,5000)
    dc.markAll();
    CHECK(dc.any());

    const auto ranges = take(dc);
    REQUIRE(ranges.size() == 1u);
    CHECK(ranges[0].firstEntry == 0u);
    CHECK(ranges[0].entryCount == 5000u); // coalesced + clamped
}

TEST_CASE("VT dirty chunks: takeRanges clears state")
{
    VTDirtyChunks dc;
    dc.reset(10000);
    dc.markEntry(5000);
    CHECK(dc.any());

    const auto first = take(dc);
    CHECK(first.size() == 1u);
    CHECK(dc.any() == false);

    // Second take with no new marks yields nothing.
    CHECK(take(dc).empty());
}

TEST_CASE("VT dirty chunks: out-of-range marks are ignored")
{
    VTDirtyChunks dc;
    dc.reset(4096); // single chunk, entries [0, 4096)
    dc.markEntry(4096);   // == totalEntries -> out of range
    dc.markEntry(999999); // far out of range
    dc.markRange(4096, 5); // firstEntry >= totalEntries -> ignored
    dc.markRange(100, 0);  // zero count -> ignored
    CHECK(dc.any() == false);
    CHECK(take(dc).empty());

    // A valid mark near the end still lands.
    dc.markEntry(4095);
    CHECK(dc.any());
    const auto ranges = take(dc);
    REQUIRE(ranges.size() == 1u);
    CHECK(ranges[0].firstEntry == 0u);
    CHECK(ranges[0].entryCount == 4096u);
}

TEST_CASE("VT dirty chunks: reset clears prior dirty state and resizes")
{
    VTDirtyChunks dc;
    dc.reset(10000);
    dc.markAll();
    CHECK(dc.any());

    dc.reset(8000); // fresh, smaller table
    CHECK(dc.any() == false);
    CHECK(dc.chunkCount() == 2u); // ceil(8000/4096)
    CHECK(take(dc).empty());
}
