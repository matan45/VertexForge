#include <doctest.h>
#include <render/virtualtexture/svt/SVTReadBatch.hpp>
#include <render/virtualtexture/svt/SVTTailMip.hpp>
#include <render/virtualtexture/VTResidencyCore.hpp>

#include <unordered_set>
#include <vector>
#include <algorithm>
#include <functional>
#include <cstdint>

// ============================================================================
// VK-1480 SVT async I/O — pure helpers behind the render-thread plan/drain of
// streamed material pages: read-plan grouping (coalesce reads by imageId+mip
// within a page budget), the frame-rotated staging-ring offset, the drain-time
// re-validation decision table, and the tail-only fallback mip rule. All CPU-only
// (no Vulkan, no disk).
// ============================================================================

using namespace render::gpudriven;
using render::vt::VTPageKey;

namespace
{
    VTPageKey K(uint32_t img, uint32_t mip, uint32_t x, uint32_t y) { return VTPageKey{img, mip, x, y}; }
}

TEST_CASE("SVT read plan: coalesces pages sharing (imageId, mip) into one read each")
{
    const std::vector<VTPageKey> req = {
        K(0, 0, 0, 0), K(0, 0, 1, 0), // img0 mip0 -> one group, two pages
        K(0, 1, 0, 0),                // img0 mip1 -> one group
        K(1, 0, 0, 0)                 // img1 mip0 -> one group
    };

    const auto groups = svtBuildReadGroups(req, /*pageBudget*/ 16);
    REQUIRE(groups.size() == 3);
    CHECK(groups[0].imageId == 0);
    CHECK(groups[0].mip == 0);
    CHECK(groups[0].pages.size() == 2); // both mip-0 pages share ONE readMipLevel
    CHECK(groups[1].imageId == 0);
    CHECK(groups[1].mip == 1);
    CHECK(groups[2].imageId == 1);

    // Total pages across groups equals the input (nothing dropped under an ample budget).
    size_t total = 0;
    for (const auto& g : groups) total += g.pages.size();
    CHECK(total == req.size());
}

TEST_CASE("SVT read plan: respects the page budget (pages, not groups)")
{
    const std::vector<VTPageKey> req = {
        K(0, 0, 0, 0), K(0, 0, 1, 0), // 2 pages, one group
        K(0, 1, 0, 0),                // 1 page, second group
        K(1, 0, 0, 0)                 // 1 page, third group
    };

    // Budget 2 -> only the first group's two pages.
    auto g2 = svtBuildReadGroups(req, 2);
    size_t t2 = 0; for (const auto& g : g2) t2 += g.pages.size();
    CHECK(t2 == 2);
    REQUIRE(g2.size() == 1);
    CHECK(g2[0].mip == 0);

    // Budget 3 -> first group (2) + start of the second (1).
    auto g3 = svtBuildReadGroups(req, 3);
    size_t t3 = 0; for (const auto& g : g3) t3 += g.pages.size();
    CHECK(t3 == 3);
    CHECK(g3.size() == 2);

    // Budget 0 -> nothing.
    CHECK(svtBuildReadGroups(req, 0).empty());
    CHECK(svtBuildReadGroups({}, 8).empty());
}

TEST_CASE("SVT read plan: unsorted input is partitioned by (imageId, mip)")
{
    const std::vector<VTPageKey> req = {
        K(2, 1, 0, 0), K(0, 0, 0, 0), K(2, 1, 1, 0), K(0, 0, 1, 0)
    };
    const auto groups = svtBuildReadGroups(req, 16);
    REQUIRE(groups.size() == 2);
    CHECK(groups[0].imageId == 0); // sorted ahead of image 2
    CHECK(groups[0].pages.size() == 2);
    CHECK(groups[1].imageId == 2);
    CHECK(groups[1].pages.size() == 2);
}

TEST_CASE("SVT submit: in-flight pages are filtered before grouping (no double submit)")
{
    // The manager inserts submitted page keys into `inFlight` and skips them next frame. Model that
    // filter over the real grouping helper: an in-flight page must not reappear in any read group.
    std::unordered_set<uint64_t> inFlight;
    inFlight.insert(K(0, 0, 0, 0).packed());

    const std::vector<VTPageKey> requested = {K(0, 0, 0, 0), K(0, 0, 1, 0)};
    std::vector<VTPageKey> toSubmit;
    for (const auto& k : requested)
        if (!inFlight.count(k.packed()))
            toSubmit.push_back(k);

    const auto groups = svtBuildReadGroups(toSubmit, 16);
    REQUIRE(groups.size() == 1);
    REQUIRE(groups[0].pages.size() == 1);
    CHECK(groups[0].pages[0].packed() == K(0, 0, 1, 0).packed()); // only the not-in-flight page
}

TEST_CASE("SVT drain decision: full re-validation table")
{
    // Accept when epoch matches, image exists, and the page isn't already resident.
    CHECK(svtDrainDecision(/*tileEpoch*/ 5, /*current*/ 5, /*imageId*/ 0, /*count*/ 2, /*resident*/ false)
          == SVTDrainDecision::Accept);
    // A tile produced before an SVT reset/toggle is stale.
    CHECK(svtDrainDecision(4, 5, 0, 2, false) == SVTDrainDecision::DropEpoch);
    // Owning image no longer exists.
    CHECK(svtDrainDecision(5, 5, 5, 2, false) == SVTDrainDecision::DropImageGone);
    // Page already resident (e.g. pinned earlier).
    CHECK(svtDrainDecision(5, 5, 0, 2, true) == SVTDrainDecision::DropResident);
    // Epoch is checked first: a stale-AND-out-of-range tile drops as stale.
    CHECK(svtDrainDecision(4, 5, 99, 2, true) == SVTDrainDecision::DropEpoch);
}

TEST_CASE("SVT teardown-with-pending leaves no dangling state")
{
    // Model the async lifecycle with an injected read function (no disk, no Vulkan): submit inserts
    // in-flight keys and enqueues results; a teardown bumps the epoch and clears queues. Any result
    // produced under the old epoch must be dropped and no in-flight key may survive teardown.
    struct Tile { VTPageKey key; uint64_t epoch; std::vector<uint8_t> bytes; };
    std::unordered_set<uint64_t> inFlight;
    std::vector<Tile> completed;
    uint64_t epoch = 1;

    std::function<std::vector<uint8_t>(uint32_t)> readFn =
        [](uint32_t /*mip*/) { return std::vector<uint8_t>(16, 0xAB); };

    const std::vector<VTPageKey> req = {K(0, 0, 0, 0), K(0, 0, 1, 0)};
    for (const auto& g : svtBuildReadGroups(req, 16))
        for (const auto& k : g.pages)
        {
            inFlight.insert(k.packed());
            completed.push_back({k, epoch, readFn(g.mip)});
        }
    CHECK(inFlight.size() == 2);
    CHECK(completed.size() == 2);

    // Teardown: invalidate outstanding results, then clear.
    ++epoch;
    uint32_t dropped = 0;
    for (const auto& t : completed)
        if (svtDrainDecision(t.epoch, epoch, t.key.imageId, /*count*/ 1, false) == SVTDrainDecision::DropEpoch)
            ++dropped;
    CHECK(dropped == completed.size()); // every pre-teardown result is stale
    inFlight.clear();
    completed.clear();
    CHECK(inFlight.empty());
    CHECK(completed.empty());
}

TEST_CASE("SVT staging-ring offset: no overlap across frames x slots")
{
    constexpr uint32_t framesInFlight = 2;
    constexpr uint32_t pagesPerFrame = 32;
    constexpr uint32_t tileBytes = 16384; // BC7 128x128 tile

    std::vector<uint64_t> offsets;
    for (uint32_t f = 0; f < framesInFlight; ++f)
        for (uint32_t s = 0; s < pagesPerFrame; ++s)
            offsets.push_back(svtStagingOffset(f, s, pagesPerFrame, tileBytes));

    // Every [off, off+tileBytes) region is disjoint: sorted offsets step by exactly tileBytes.
    std::sort(offsets.begin(), offsets.end());
    for (size_t i = 1; i < offsets.size(); ++i)
        CHECK(offsets[i] - offsets[i - 1] == tileBytes);

    // Frame 1 slot 0 starts exactly one frame-block past frame 0 slot 0.
    CHECK(svtStagingOffset(1, 0, pagesPerFrame, tileBytes)
          == static_cast<uint64_t>(pagesPerFrame) * tileBytes);
    // The whole ring fits framesInFlight * pagesPerFrame tiles.
    const uint64_t last = svtStagingOffset(framesInFlight - 1, pagesPerFrame - 1, pagesPerFrame, tileBytes);
    CHECK(last + tileBytes == static_cast<uint64_t>(framesInFlight) * pagesPerFrame * tileBytes);
}

TEST_CASE("SVT tail-only mip: first level with longer edge <= 128")
{
    // 4096x4096, full 13-mip chain -> mip 5 is 128px (mip 4 is 256).
    CHECK(svtTailStartMip(4096, 4096, 13) == 5);

    // Non-square edge: 513x512. 513>>2 = 128 -> S = 2.
    CHECK(svtTailStartMip(513, 512, 10) == 2);

    // A truncated chain that never reaches <=128 falls back to the coarsest stored level.
    CHECK(svtTailStartMip(1024, 1024, 2) == 1); // mips: 1024, 512 (both > 128) -> mipLevels-1
    CHECK(svtTailStartMip(4096, 4096, 3) == 2); // 4096,2048,1024 all > 128 -> coarsest

    // Already small: mip 0 already <= 128.
    CHECK(svtTailStartMip(128, 128, 8) == 0);
    CHECK(svtTailStartMip(100, 64, 4) == 0);

    // Degenerate: no mips.
    CHECK(svtTailStartMip(2048, 2048, 0) == 0);
}
