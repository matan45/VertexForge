#include <doctest.h>
#include <render/virtualtexture/VTPoolAllocator.hpp>
#include <render/virtualtexture/VTTypes.hpp>
#include <render/gpudriven/terrain/TerrainRVTLayout.hpp>

// ============================================================================
// VTPoolAllocator + pool-sizing math (VK-1209). Pure, CPU-only.
// ============================================================================

using namespace render::vt;

TEST_CASE("VTPoolAllocator: LIFO alloc/free and exhaustion")
{
    VTPoolAllocator a;
    a.init(4);
    CHECK(a.capacityCount() == 4);
    CHECK(a.freeCount() == 4);
    CHECK(a.allocatedCount() == 0);

    // Seeded reversed so the first allocations hand out the lowest tile indices.
    CHECK(a.allocate() == 0);
    CHECK(a.allocate() == 1);
    CHECK(a.allocate() == 2);
    CHECK(a.allocate() == 3);
    CHECK(a.allocatedCount() == 4);
    CHECK(a.allocate() == VT_INVALID_TILE); // exhausted

    // Free one; LIFO returns it on the next allocation.
    CHECK(a.free(2) == true);
    CHECK(a.freeCount() == 1);
    CHECK(a.allocate() == 2);
}

TEST_CASE("VTPoolAllocator: double-free and out-of-range are rejected")
{
    VTPoolAllocator a;
    a.init(2);
    CHECK(a.allocate() == 0);
    CHECK(a.isAllocated(0));
    CHECK(a.free(0) == true);
    CHECK(a.free(0) == false);  // double free
    CHECK(a.free(99) == false); // out of range
    CHECK(a.free(1) == false);  // never allocated
    CHECK(a.freeCount() == 2);  // tile 0 returned + tile 1 still free
}

TEST_CASE("VTPoolAllocator: freeAll resets to full capacity")
{
    VTPoolAllocator a;
    a.init(3);
    (void)a.allocate();
    (void)a.allocate();
    CHECK(a.allocatedCount() == 2);
    a.freeAll();
    CHECK(a.freeCount() == 3);
    CHECK(a.allocate() == 0);
}

TEST_CASE("VT pool sizing: budget -> pool dim -> tile count")
{
    // 128 MB, 2 planes (RVT MRT), 4 B/texel -> 4096 edge -> 32 tiles/side -> 1024 tiles.
    CHECK(vtPoolDimForBudget(128, 2, 4) == 4096);
    CHECK(vtTilesPerSide(4096) == 32);
    CHECK(vtMaxTiles(4096) == 1024);

    // Result is always a multiple of the page size and never below one page.
    CHECK(vtPoolDimForBudget(256, 1, 4) % VT_PAGE_SIZE == 0);
    CHECK(vtPoolDimForBudget(0, 2, 4) == VT_PAGE_SIZE);
    CHECK(vtPoolDimForBudget(1, 2, 4) >= VT_PAGE_SIZE);

    // Exact byte accounting for the 128 MB / 2-plane case.
    const uint32_t dim = vtPoolDimForBudget(128, 2, 4);
    const uint64_t bytes = static_cast<uint64_t>(dim) * dim * 2ull * 4ull;
    CHECK(bytes == (128ull << 20));
}

TEST_CASE("Terrain RVT layout: detail maps add normal and HDR emission planes")
{
    const auto legacyLayout = render::gpudriven::terrainRVTLayout(false);
    REQUIRE(legacyLayout.planeFormats.size() == 2);
    CHECK(legacyLayout.bytesPerTexel == 8);
    CHECK(legacyLayout.planeFormats[0] == vk::Format::eR8G8B8A8Srgb);
    CHECK(legacyLayout.planeFormats[1] == vk::Format::eR8G8B8A8Unorm);

    const auto detailLayout = render::gpudriven::terrainRVTLayout(true);
    REQUIRE(detailLayout.planeFormats.size() == 4);
    CHECK(detailLayout.bytesPerTexel == 20);
    CHECK(detailLayout.planeFormats[0] == vk::Format::eR8G8B8A8Srgb);
    CHECK(detailLayout.planeFormats[1] == vk::Format::eR8G8B8A8Unorm);
    CHECK(detailLayout.planeFormats[2] == vk::Format::eR8G8B8A8Unorm);
    CHECK(detailLayout.planeFormats[3] == vk::Format::eR16G16B16A16Sfloat);

    // A fixed 128 MiB budget retains the current atlas edge when detail maps are off and
    // shrinks to account for the 12 extra bytes/texel when they are on.
    CHECK(vtPoolDimForBudget(128, 1, legacyLayout.bytesPerTexel) == 4096);
    CHECK(vtPoolDimForBudget(128, 1, detailLayout.bytesPerTexel) == 2560);
}
