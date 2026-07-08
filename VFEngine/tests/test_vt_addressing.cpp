#include <doctest.h>
#include <render/virtualtexture/VTTypes.hpp>
#include <render/virtualtexture/VTAddressing.hpp>

#include <vector>

// ============================================================================
// VT page-entry packing + mip-pyramid math + virtual->physical addressing
// (VK-1209). This is the C++ side of the parity contract with
// resources/shaders/common/vt_sampling.glsl. Pure, CPU-only.
// ============================================================================

using namespace render::vt;

TEST_CASE("VT page entry: pack/unpack round trip")
{
    const uint32_t xs[] = {0u, 1u, 63u, 100u, 4095u};
    const uint32_t ys[] = {0u, 1u, 63u, 1000u, 4095u};
    for (uint32_t tx : xs)
        for (uint32_t ty : ys)
        {
            const uint32_t e = vtPackPageEntry(tx, ty);
            CHECK(vtIsPageValid(e));
            uint32_t rx = 0, ry = 0;
            vtUnpackPageEntry(e, rx, ry);
            CHECK(rx == tx);
            CHECK(ry == ty);
        }

    CHECK(vtIsPageValid(0u) == false); // a zeroed entry is "not resident"
}

TEST_CASE("VT mip pyramid: counts, offsets and coverage")
{
    // 256 pages/side -> 9 mips, mip8 is a single page (the plan's 4 km example).
    CHECK(vtComputeMipCount(256, 256) == 9);
    CHECK(vtPagesAtMip(256, 0) == 256);
    CHECK(vtPagesAtMip(256, 8) == 1);

    // Sub-offsets are the running sum of per-mip page counts: monotone, non-overlapping,
    // and summing to the block total.
    const uint32_t mipCount = vtComputeMipCount(256, 256);
    uint32_t running = 0;
    for (uint32_t m = 0; m < mipCount; ++m)
    {
        CHECK(vtMipSubOffset(256, 256, m) == running);
        running += vtPagesAtMip(256, m) * vtPagesAtMip(256, m);
    }
    CHECK(vtBlockEntryCount(256, 256, mipCount) == running);
    CHECK(running == 87381u); // (4^9 - 1) / 3

    // Ceil rule never under-covers a non-power-of-two image.
    CHECK(vtPagesAtMip(5, 1) == 3);
    CHECK(vtPagesAtMip(5, 2) == 2);
    CHECK(vtPagesAtMip(5, 3) == 1);
    CHECK(vtComputeMipCount(5, 5) == 4);

    // Linear index is contiguous and within the block.
    VTImageDesc img;
    img.pagesX0 = 4;
    img.pagesY0 = 4;
    img.computeMips();
    CHECK(img.mipCount == 3); // 4 -> 2 -> 1
    CHECK(vtPageLinearIndex(4, 4, 0, 1, 2) == 0u + 2u * 4u + 1u); // mip0 row-major
    CHECK(vtPageLinearIndex(4, 4, 2, 0, 0) == img.blockEntryCount() - 1u); // last entry = coarsest page

    // vtDecodeEntry is the exact inverse over every entry of the pyramid.
    for (uint32_t m = 0; m < img.mipCount; ++m)
    {
        const uint32_t px = vtPagesAtMip(img.pagesX0, m);
        const uint32_t py = vtPagesAtMip(img.pagesY0, m);
        for (uint32_t yy = 0; yy < py; ++yy)
            for (uint32_t xx = 0; xx < px; ++xx)
            {
                const uint32_t idx = vtPageLinearIndex(img.pagesX0, img.pagesY0, m, xx, yy);
                uint32_t dm = 99, dx = 99, dy = 99;
                CHECK(vtDecodeEntry(img.pagesX0, img.pagesY0, img.mipCount, idx, dm, dx, dy));
                CHECK(dm == m);
                CHECK(dx == xx);
                CHECK(dy == yy);
            }
    }
    uint32_t om = 0, ox = 0, oy = 0;
    CHECK(vtDecodeEntry(4, 4, img.mipCount, img.blockEntryCount(), om, ox, oy) == false); // out of range
}

TEST_CASE("VT addressing: fine->coarse fallback always resolves against a pinned coarse page")
{
    VTImageDesc img;
    img.pagesX0 = 4;
    img.pagesY0 = 4;
    img.computeMips();
    img.pageTableBase = 0;

    std::vector<uint32_t> table(img.blockEntryCount(), 0u); // all invalid

    // Only the coarsest mip (a single page) is resident, mapped to physical tile (2,3).
    const uint32_t coarseMip = img.mipCount - 1u;
    const uint32_t coarseIdx = img.pageTableBase + vtMipSubOffset(img.pagesX0, img.pagesY0, coarseMip);
    table[coarseIdx] = vtPackPageEntry(2, 3);

    const uint32_t poolDim = 512; // 4 tiles per side

    // A lookup at the finest requested mip must walk up to the resident coarse page.
    const auto s = VTAddressing::lookup(img, table.data(), poolDim, 0.3f, 0.7f, 0);
    CHECK(s.valid);
    CHECK(s.residentMip == coarseMip);

    // Physical UV lands inside tile (2,3)'s bordered interior.
    const float u0 = static_cast<float>(2u * VT_PAGE_SIZE + VT_BORDER) / poolDim;
    const float u1 = static_cast<float>(2u * VT_PAGE_SIZE + VT_BORDER + VT_PAGE_INTERIOR) / poolDim;
    const float v0 = static_cast<float>(3u * VT_PAGE_SIZE + VT_BORDER) / poolDim;
    const float v1 = static_cast<float>(3u * VT_PAGE_SIZE + VT_BORDER + VT_PAGE_INTERIOR) / poolDim;
    CHECK(s.u >= u0);
    CHECK(s.u <= u1);
    CHECK(s.v >= v0);
    CHECK(s.v <= v1);

    // Making the finest page resident makes the walk stop there instead.
    const uint32_t fineIdx = img.pageTableBase + vtPageLinearIndex(img.pagesX0, img.pagesY0, 0,
                                                                   /*x*/1, /*y*/2); // (0.3,0.7)@mip0
    table[fineIdx] = vtPackPageEntry(0, 0);
    const auto s2 = VTAddressing::lookup(img, table.data(), poolDim, 0.3f, 0.7f, 0);
    CHECK(s2.valid);
    CHECK(s2.residentMip == 0u);

    // desiredMip past the pyramid clamps to the coarsest level (still resolves).
    const auto s3 = VTAddressing::lookup(img, table.data(), poolDim, 0.9f, 0.9f, 99);
    CHECK(s3.valid);
}

TEST_CASE("VT addressing: invalid inputs and empty table")
{
    VTImageDesc img;
    img.pagesX0 = 2;
    img.pagesY0 = 2;
    img.computeMips();
    std::vector<uint32_t> table(img.blockEntryCount(), 0u); // nothing resident

    // Nothing resident anywhere -> no resolution.
    const auto s = VTAddressing::lookup(img, table.data(), 256, 0.5f, 0.5f, 0);
    CHECK(s.valid == false);

    // Degenerate pool dim is rejected.
    const auto s2 = VTAddressing::lookup(img, table.data(), 0, 0.5f, 0.5f, 0);
    CHECK(s2.valid == false);
}

TEST_CASE("VT coarse-tail pin count (VK-1480)")
{
    // Full mip chain: the coarsest level is always a single page.
    CHECK(vtCoarsePinPageCount(4, 4, vtComputeMipCount(4, 4)) == 1);
    CHECK(vtCoarsePinPageCount(5, 3, vtComputeMipCount(5, 3)) == 1);
    CHECK(vtCoarsePinPageCount(256, 256, vtComputeMipCount(256, 256)) == 1);

    // Truncated source chain (SVT clamps mipCount to stored levels): the coarsest STORED level spans
    // multiple pages, so a safe pin must cover them all.
    CHECK(vtCoarsePinPageCount(4, 4, 2) == 2u * 2u); // mip1 of a 4x4-page image = 2x2 pages
    CHECK(vtCoarsePinPageCount(8, 4, 1) == 8u * 4u); // only mip0 stored: the whole grid

    // Degenerate: zero mips pins nothing.
    CHECK(vtCoarsePinPageCount(4, 4, 0) == 0u);

    // The full-chain pin set is always within the registration cap; a wide truncated one can exceed it.
    CHECK(vtCoarsePinPageCount(64, 64, vtComputeMipCount(64, 64)) <= VT_MAX_PIN_PAGES);
    CHECK(vtCoarsePinPageCount(8, 4, 1) > VT_MAX_PIN_PAGES); // 32 > 16 -> registration refuses
}

TEST_CASE("VT split pool budget (VK-1480)")
{
    // Even split; the halves always sum to the total.
    auto a = vtSplitPoolBudget(256);
    CHECK(a.firstMB == 128);
    CHECK(a.secondMB == 128);
    CHECK(a.firstMB + a.secondMB == 256);

    // Odd total: remainder goes to the first pool.
    auto b = vtSplitPoolBudget(255);
    CHECK(b.firstMB == 128);
    CHECK(b.secondMB == 127);
    CHECK(b.firstMB + b.secondMB == 255);

    auto c = vtSplitPoolBudget(1);
    CHECK(c.firstMB == 1);
    CHECK(c.secondMB == 0);

    auto z = vtSplitPoolBudget(0);
    CHECK(z.firstMB == 0);
    CHECK(z.secondMB == 0);
}

TEST_CASE("VT addressing: desiredMipFromDerivatives")
{
    // No movement -> mip 0.
    CHECK(VTAddressing::desiredMipFromDerivatives(0, 0, 0, 0, 1024.0f) == doctest::Approx(0.0f));
    // ~1 texel per pixel -> mip 0.
    CHECK(VTAddressing::desiredMipFromDerivatives(1.0f / 1024.0f, 0, 0, 1.0f / 1024.0f, 1024.0f)
          == doctest::Approx(0.0f));
    // 2 texels per pixel -> mip 1.
    CHECK(VTAddressing::desiredMipFromDerivatives(2.0f / 1024.0f, 0, 0, 0, 1024.0f)
          == doctest::Approx(1.0f));
    // 4 texels per pixel -> mip 2.
    CHECK(VTAddressing::desiredMipFromDerivatives(0, 0, 0, 4.0f / 1024.0f, 1024.0f)
          == doctest::Approx(2.0f));
}
