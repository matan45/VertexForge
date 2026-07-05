#include <doctest.h>
#include <render/virtualtexture/VTResidencyCore.hpp>

#include <vector>

// ============================================================================
// VTResidencyCore streaming policy (VK-1209): request diff, per-frame budget,
// LRU + age-gated eviction, pinned pages, protect requested-this-frame. Pure,
// CPU-only. Mirrors VSM's residency policy without a Vulkan device.
// ============================================================================

using namespace render::vt;

namespace
{
    VTPageKey K(uint32_t img, uint32_t mip, uint32_t x, uint32_t y)
    {
        return VTPageKey{img, mip, x, y};
    }
}

TEST_CASE("VT residency: allocate misses, respect the per-frame cap")
{
    VTResidencyCore core;
    const std::vector<VTPageKey> req = {K(0, 0, 0, 0), K(0, 0, 1, 0), K(0, 0, 2, 0), K(0, 0, 3, 0)};

    const auto plan = core.planFrame(req, /*frame*/ 10, /*freeTiles*/ 100, /*pagesPerFrame*/ 2, /*age*/ 60);
    CHECK(plan.toAllocate.size() == 2);
    CHECK(plan.toEvict.empty());
    CHECK(plan.toAllocate[0].packed() == K(0, 0, 0, 0).packed()); // requested order preserved
    CHECK(plan.toAllocate[1].packed() == K(0, 0, 1, 0).packed());
}

TEST_CASE("VT residency: resident requests are touched, not re-allocated")
{
    VTResidencyCore core;
    core.commitAllocation(K(0, 0, 0, 0), /*tile*/ 5, /*frame*/ 1, /*pinned*/ false);

    const std::vector<VTPageKey> req = {K(0, 0, 0, 0), K(0, 0, 1, 0)};
    const auto plan = core.planFrame(req, 10, 100, 8, 60);

    CHECK(plan.toAllocate.size() == 1);
    CHECK(plan.toAllocate[0].packed() == K(0, 0, 1, 0).packed());
    CHECK(core.tileFor(K(0, 0, 0, 0)) == 5);
}

TEST_CASE("VT residency: dedups repeated requests")
{
    VTResidencyCore core;
    const std::vector<VTPageKey> req = {K(0, 0, 7, 7), K(0, 0, 7, 7), K(0, 0, 7, 7)};
    const auto plan = core.planFrame(req, 10, 100, 8, 60);
    CHECK(plan.toAllocate.size() == 1);
}

TEST_CASE("VT residency: evict LRU aged-out page under tile pressure")
{
    VTResidencyCore core;
    core.commitAllocation(K(0, 0, 0, 0), 0, /*frame*/ 1, false); // oldest
    core.commitAllocation(K(0, 0, 1, 0), 1, /*frame*/ 5, false); // newer

    const std::vector<VTPageKey> req = {K(0, 0, 9, 9)};
    const auto plan = core.planFrame(req, /*frame*/ 100, /*freeTiles*/ 0, /*cap*/ 4, /*age*/ 10);

    CHECK(plan.toAllocate.size() == 1);
    REQUIRE(plan.toEvict.size() == 1);
    CHECK(plan.toEvict[0].packed() == K(0, 0, 0, 0).packed()); // oldest lastUsedFrame first
}

TEST_CASE("VT residency: pinned pages are never evicted")
{
    VTResidencyCore core;
    core.commitAllocation(K(0, 0, 0, 0), 0, /*frame*/ 1, /*pinned*/ true); // e.g. pinned coarse RVT mip

    const std::vector<VTPageKey> req = {K(0, 0, 9, 9)};
    const auto plan = core.planFrame(req, 100, /*freeTiles*/ 0, 4, /*age*/ 10);

    CHECK(plan.toEvict.empty());
    CHECK(plan.toAllocate.empty()); // 0 free + 0 evictable = no room; page stays blurry via fallback
}

TEST_CASE("VT residency: a page requested this frame is protected from eviction")
{
    VTResidencyCore core;
    core.commitAllocation(K(0, 0, 0, 0), 0, /*frame*/ 1, false); // old, but re-requested this frame
    core.commitAllocation(K(0, 0, 1, 0), 1, /*frame*/ 1, false); // old, not requested

    const std::vector<VTPageKey> req = {K(0, 0, 0, 0), K(0, 0, 9, 9)};
    const auto plan = core.planFrame(req, 100, /*freeTiles*/ 0, 4, /*age*/ 10);

    REQUIRE(plan.toAllocate.size() == 1);
    CHECK(plan.toAllocate[0].packed() == K(0, 0, 9, 9).packed());
    REQUIRE(plan.toEvict.size() == 1);
    CHECK(plan.toEvict[0].packed() == K(0, 0, 1, 0).packed()); // the un-requested old page
}

TEST_CASE("VT residency: young pages don't evict (blur instead of thrash)")
{
    VTResidencyCore core;
    core.commitAllocation(K(0, 0, 0, 0), 0, /*frame*/ 95, false); // used 5 frames ago

    const std::vector<VTPageKey> req = {K(0, 0, 9, 9)};
    const auto plan = core.planFrame(req, /*frame*/ 100, /*freeTiles*/ 0, 4, /*age*/ 10);

    CHECK(plan.toEvict.empty()); // (100-95)=5 <= 10, not aged out
    CHECK(plan.toAllocate.empty());
}

TEST_CASE("VT residency: pinned pages survive maximal pressure (VK-1480 coarse-tail pins)")
{
    VTResidencyCore core;
    // A cluster of pinned coarse-tail pages (as SVT pins them) plus one evictable page.
    for (uint32_t i = 0; i < 8; ++i)
        core.commitAllocation(K(0, 5, i, 0), /*tile*/ i, /*frame*/ 1, /*pinned*/ true);
    core.commitAllocation(K(0, 0, 9, 9), /*tile*/ 8, /*frame*/ 1, /*pinned*/ false); // aged, evictable

    // Flood with new requests and zero free tiles: only the single unpinned page may be evicted.
    std::vector<VTPageKey> req;
    for (uint32_t i = 0; i < 20; ++i)
        req.push_back(K(1, 0, i, 0));

    const auto plan = core.planFrame(req, /*frame*/ 1000, /*freeTiles*/ 0, /*cap*/ 32, /*age*/ 10);

    // No pinned page appears in the eviction list.
    for (const auto& e : plan.toEvict)
        CHECK((e.mip != 5 || e.imageId != 0)); // the pinned cluster is img0 mip5
    CHECK(plan.toEvict.size() == 1); // only the one unpinned aged page can go
    CHECK(plan.toEvict[0].packed() == K(0, 0, 9, 9).packed());
    // Exactly one tile freed -> at most one new allocation this frame.
    CHECK(plan.toAllocate.size() == 1);
}

TEST_CASE("VT residency: a pinned page re-requested via feedback is not double-allocated")
{
    VTResidencyCore core;
    core.commitAllocation(K(3, 4, 0, 0), /*tile*/ 2, /*frame*/ 1, /*pinned*/ true);

    // Feedback re-requests the (already resident, pinned) coarse page alongside a genuine miss.
    const std::vector<VTPageKey> req = {K(3, 4, 0, 0), K(3, 0, 1, 1)};
    const auto plan = core.planFrame(req, /*frame*/ 50, /*freeTiles*/ 100, /*cap*/ 8, /*age*/ 10);

    REQUIRE(plan.toAllocate.size() == 1);                            // only the real miss
    CHECK(plan.toAllocate[0].packed() == K(3, 0, 1, 1).packed());
    CHECK(core.tileFor(K(3, 4, 0, 0)) == 2);                         // pin keeps its tile
    CHECK(plan.toEvict.empty());
}

TEST_CASE("VT residency: commit + evict round trip")
{
    VTResidencyCore core;
    CHECK(core.residentCount() == 0);

    core.commitAllocation(K(1, 2, 3, 4), 7, 10, false);
    CHECK(core.residentCount() == 1);
    CHECK(core.isResident(K(1, 2, 3, 4)));
    CHECK(core.tileFor(K(1, 2, 3, 4)) == 7);

    CHECK(core.commitEviction(K(1, 2, 3, 4)) == 7);
    CHECK(core.residentCount() == 0);
    CHECK(core.commitEviction(K(1, 2, 3, 4)) == VT_INVALID_TILE);
}
