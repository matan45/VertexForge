#include <doctest.h>
#include <render/virtualtexture/VTPageExtract.hpp>

#include <vector>
#include <algorithm>

// ============================================================================
// VT SVT page extraction (VK-1209 Phase 2): block-aligned tile copy from a source
// mip with edge clamp. Pure, CPU-only.
// ============================================================================

using namespace render::vt;

namespace
{
    // A synthetic mip where each block's first byte encodes its linear block index.
    std::vector<uint8_t> makeMip(uint32_t w, uint32_t h, const VTBlockFormat& fmt, uint32_t& bpsX, uint32_t& bpsY)
    {
        bpsX = vtBlocksPerAxis(w, fmt);
        bpsY = vtBlocksPerAxis(h, fmt);
        std::vector<uint8_t> mip(static_cast<size_t>(bpsX) * bpsY * fmt.blockBytes, 0u);
        for (uint32_t by = 0; by < bpsY; ++by)
            for (uint32_t bx = 0; bx < bpsX; ++bx)
                mip[(static_cast<size_t>(by) * bpsX + bx) * fmt.blockBytes] = static_cast<uint8_t>((by * bpsX + bx) & 0xFFu);
        return mip;
    }
}

TEST_CASE("VT page extraction: sizing helpers")
{
    CHECK(vtBlocksPerAxis(256, VT_FORMAT_BC7) == 64);
    CHECK(vtBlocksPerAxis(255, VT_FORMAT_BC7) == 64); // ceil
    CHECK(vtBlocksPerAxis(256, VT_FORMAT_RGBA8) == 256);
    CHECK(vtTileByteSize(VT_FORMAT_BC7) == (VT_PAGE_SIZE / 4) * (VT_PAGE_SIZE / 4) * 16); // 32*32*16 = 16384
    CHECK(vtTileByteSize(VT_FORMAT_RGBA8) == VT_PAGE_SIZE * VT_PAGE_SIZE * 4);            // 128*128*4
}

TEST_CASE("VT page extraction: interior page copies the right BC7 blocks")
{
    const uint32_t W = 256, H = 256;
    const VTBlockFormat fmt = VT_FORMAT_BC7;
    uint32_t bpsX = 0, bpsY = 0;
    const std::vector<uint8_t> mip = makeMip(W, H, fmt, bpsX, bpsY);
    const uint32_t tb = VT_PAGE_SIZE / fmt.blockTexels; // 32

    auto srcId = [&](int32_t sbx, int32_t sby)
    {
        sbx = std::clamp(sbx, 0, static_cast<int32_t>(bpsX) - 1);
        sby = std::clamp(sby, 0, static_cast<int32_t>(bpsY) - 1);
        return static_cast<uint8_t>((sby * bpsX + sbx) & 0xFFu);
    };

    std::vector<uint8_t> tile;
    REQUIRE(vtExtractTile(mip.data(), static_cast<uint32_t>(mip.size()), W, H, fmt, /*pageX*/ 1, /*pageY*/ 1, tile));
    CHECK(tile.size() == vtTileByteSize(fmt));

    // page (1,1): startTexel = 1*120 - 4 = 116 -> startBlock = 29. All in range.
    CHECK(tile[0] == srcId(29, 29));                              // out block (0,0)
    CHECK(tile[static_cast<size_t>(tb - 1) * fmt.blockBytes] == srcId(29 + 31, 29)); // out (31,0)
    CHECK(tile[static_cast<size_t>(tb) * tb * fmt.blockBytes - fmt.blockBytes] == srcId(29 + 31, 29 + 31)); // out (31,31)
}

TEST_CASE("VT page extraction: low-edge page replicates the border block")
{
    const uint32_t W = 256, H = 256;
    const VTBlockFormat fmt = VT_FORMAT_BC7;
    uint32_t bpsX = 0, bpsY = 0;
    const std::vector<uint8_t> mip = makeMip(W, H, fmt, bpsX, bpsY);

    auto srcId = [&](int32_t sbx, int32_t sby)
    {
        sbx = std::clamp(sbx, 0, static_cast<int32_t>(bpsX) - 1);
        sby = std::clamp(sby, 0, static_cast<int32_t>(bpsY) - 1);
        return static_cast<uint8_t>((sby * bpsX + sbx) & 0xFFu);
    };

    std::vector<uint8_t> tile;
    REQUIRE(vtExtractTile(mip.data(), static_cast<uint32_t>(mip.size()), W, H, fmt, /*pageX*/ 0, /*pageY*/ 0, tile));

    // page (0,0): startBlock = -1 -> out block 0 and 1 both clamp to src block 0.
    CHECK(tile[0] == srcId(0, 0));
    CHECK(tile[1 * fmt.blockBytes] == srcId(0, 0));
    CHECK(tile[2 * fmt.blockBytes] == srcId(1, 0)); // out block 2 -> src block 1
}

TEST_CASE("VT page extraction: rejects undersized source")
{
    const VTBlockFormat fmt = VT_FORMAT_BC7;
    std::vector<uint8_t> tooSmall(16, 0u); // 1 block, but a 256x256 mip needs 64x64
    std::vector<uint8_t> tile;
    CHECK(vtExtractTile(tooSmall.data(), static_cast<uint32_t>(tooSmall.size()), 256, 256, fmt, 0, 0, tile) == false);
    CHECK(vtExtractTile(nullptr, 0, 256, 256, fmt, 0, 0, tile) == false);
}
