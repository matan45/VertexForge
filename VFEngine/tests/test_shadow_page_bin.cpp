#include <doctest.h>
#include <render/gpudriven/scene/ShadowBinPacking.hpp>

#include <vector>

// ============================================================
// buildPageBinBase — assigns a compact render slot to each VSM page that renders this frame,
// producing the pageBinBase[] table the page-binned shadow cull shader indexes and the per-page
// baseDrawIndex (renderSlot * SHADOW_BIN_CAPACITY) the recorder draws from. CPU-only.
// ============================================================

using render::gpudriven::ShadowBinPageRequest;
using render::gpudriven::buildPageBinBase;
using render::gpudriven::INVALID_SHADOW_BIN_SLOT;
using render::gpudriven::MAX_RENDERED_SHADOW_PAGES;

namespace
{
    std::vector<ShadowBinPageRequest> reqs(std::initializer_list<uint32_t> pages)
    {
        std::vector<ShadowBinPageRequest> r;
        for (uint32_t p : pages) r.push_back({p});
        return r;
    }
}

TEST_CASE("buildPageBinBase: contiguous slots in request order")
{
    auto r = reqs({5, 2, 9});
    std::vector<uint32_t> base, assigned;
    uint32_t used = buildPageBinBase(r, 16, base, &assigned);

    CHECK(used == 3);
    REQUIRE(base.size() == 16);
    CHECK(base[5] == 0u);
    CHECK(base[2] == 1u);
    CHECK(base[9] == 2u);
    // Every other page must be INVALID (falls back to legacy / not rendering).
    for (uint32_t p = 0; p < 16; ++p)
        if (p != 5 && p != 2 && p != 9)
            CHECK(base[p] == INVALID_SHADOW_BIN_SLOT);
    REQUIRE(assigned.size() == 3);
    CHECK(assigned[0] == 0u);
    CHECK(assigned[1] == 1u);
    CHECK(assigned[2] == 2u);
}

TEST_CASE("buildPageBinBase: duplicate pages reuse their slot")
{
    auto r = reqs({5, 5, 2, 5});
    std::vector<uint32_t> base, assigned;
    uint32_t used = buildPageBinBase(r, 16, base, &assigned);

    CHECK(used == 2); // only 5 and 2 are distinct
    CHECK(base[5] == 0u);
    CHECK(base[2] == 1u);
    CHECK(assigned[0] == 0u);
    CHECK(assigned[1] == 0u); // duplicate 5
    CHECK(assigned[2] == 1u);
    CHECK(assigned[3] == 0u); // duplicate 5
}

TEST_CASE("buildPageBinBase: out-of-range pages are skipped, not slotted")
{
    // Page 20 is >= totalPageCount (16): it must be skipped and NOT consume a slot.
    auto r = reqs({3, 20, 7});
    std::vector<uint32_t> base, assigned;
    uint32_t used = buildPageBinBase(r, 16, base, &assigned);

    CHECK(used == 2);
    CHECK(base[3] == 0u);
    CHECK(base[7] == 1u);
    CHECK(assigned[0] == 0u);
    CHECK(assigned[1] == INVALID_SHADOW_BIN_SLOT); // the out-of-range page
    CHECK(assigned[2] == 1u);
}

TEST_CASE("buildPageBinBase: pages past the arena fall back to legacy (INVALID)")
{
    const uint32_t total = MAX_RENDERED_SHADOW_PAGES + 10;
    std::vector<ShadowBinPageRequest> r;
    for (uint32_t p = 0; p < total; ++p) r.push_back({p});

    std::vector<uint32_t> base, assigned;
    uint32_t used = buildPageBinBase(r, total, base, &assigned);

    CHECK(used == MAX_RENDERED_SHADOW_PAGES);
    // First MAX pages get slots 0..MAX-1.
    for (uint32_t p = 0; p < MAX_RENDERED_SHADOW_PAGES; ++p)
        CHECK(base[p] == p);
    // The overflow tail is INVALID (each such page falls back to the legacy loop).
    for (uint32_t p = MAX_RENDERED_SHADOW_PAGES; p < total; ++p)
    {
        CHECK(base[p] == INVALID_SHADOW_BIN_SLOT);
        CHECK(assigned[p] == INVALID_SHADOW_BIN_SLOT);
    }
}

TEST_CASE("buildPageBinBase: empty request list assigns nothing")
{
    std::vector<ShadowBinPageRequest> r;
    std::vector<uint32_t> base, assigned;
    uint32_t used = buildPageBinBase(r, 8, base, &assigned);

    CHECK(used == 0);
    REQUIRE(base.size() == 8);
    for (uint32_t p = 0; p < 8; ++p)
        CHECK(base[p] == INVALID_SHADOW_BIN_SLOT);
    CHECK(assigned.empty());
}
