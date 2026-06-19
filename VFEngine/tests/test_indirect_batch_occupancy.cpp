// Tests for render::gpudriven::SectionOccupancy — the Device-free per-(batch, shaderGroup)
// occupancy bitmask extracted from IndirectBatchManager. This is the CPU logic that lets the
// GPU-driven renderer skip recording indirect draws for empty sections (see
// GPUDrivenRendererScene.cpp recomputeOccupancy + GPUDrivenRendererDraw.cpp sectionHasCandidates).
//
// IndirectBatchManager itself needs a real core::Device and allocates GPU buffers in init(),
// so it cannot be constructed in this CPU-only runner. The occupancy logic was extracted into
// SectionOccupancy precisely so it can be exercised here without any Vulkan dependency.

#include <doctest.h>
#include <render/gpudriven/scene/IndirectBatchManager.hpp>
#include <render/gpudriven/GPUDrivenTypes.hpp>
#include <vector>

using render::gpudriven::SectionOccupancy;
using render::gpudriven::GPUObjectData;
using render::gpudriven::SHADER_GROUP_TRANSPARENT;
using render::gpudriven::SHADER_GROUP_BLEND;
using render::gpudriven::MAX_BATCH_COUNT;
using render::gpudriven::MAX_SHADER_GROUPS;

namespace
{
    // Builds an object whose only field relevant to occupancy is shaderGroupIndex.
    GPUObjectData makeObject(uint32_t shaderGroup)
    {
        GPUObjectData obj{};
        obj.shaderGroupIndex = shaderGroup;
        return obj;
    }

    // Counts how many (batch, shaderGroup) sections report candidates, for whole-grid assertions.
    uint32_t countOccupied(const SectionOccupancy& occ, uint32_t batches, uint32_t groups)
    {
        uint32_t total = 0;
        for (uint32_t b = 0; b < batches; ++b)
            for (uint32_t g = 0; g < groups; ++g)
                if (occ.hasCandidates(b, g)) ++total;
        return total;
    }
}

TEST_CASE("SectionOccupancy: configure clears all sections")
{
    SectionOccupancy occ;
    occ.configure(4, 16);

    CHECK(occ.batchCount == 4);
    CHECK(occ.shaderGroupCount == 16);
    CHECK(countOccupied(occ, 4, 16) == 0);
}

TEST_CASE("SectionOccupancy: section indexing matches getSectionIndex formula")
{
    // sectionIndex must equal batch * shaderGroupCount + shaderGroup so it stays in lockstep
    // with IndirectBatchManager::getSectionIndex (which the renderer also uses for byte offsets).
    SectionOccupancy occ;
    occ.configure(4, 16);

    CHECK(occ.sectionIndex(0, 0) == 0);
    CHECK(occ.sectionIndex(0, 5) == 5);
    CHECK(occ.sectionIndex(1, 0) == 16);
    CHECK(occ.sectionIndex(3, 15) == 3 * 16 + 15);
}

TEST_CASE("SectionOccupancy: single opaque object occupies exactly one section")
{
    SectionOccupancy occ;
    occ.configure(4, 16);

    std::vector<GPUObjectData> objects{ makeObject(0) };  // shader group 0 (opaque)
    occ.recompute(objects, static_cast<uint32_t>(objects.size()));

    // Object index 0 -> batch (0 % 4) = 0, group 0.
    CHECK(occ.hasCandidates(0, 0));
    CHECK(countOccupied(occ, 4, 16) == 1);
}

TEST_CASE("SectionOccupancy: objects spread across batches by index modulo batchCount")
{
    SectionOccupancy occ;
    occ.configure(4, 16);

    // Six opaque objects, indices 0..5 -> batches 0,1,2,3,0,1. All shader group 0.
    std::vector<GPUObjectData> objects;
    for (int i = 0; i < 6; ++i) objects.push_back(makeObject(0));
    occ.recompute(objects, static_cast<uint32_t>(objects.size()));

    // Every batch (0..3) ends up occupied for group 0; indices 4 and 5 just re-mark 0 and 1.
    CHECK(occ.hasCandidates(0, 0));
    CHECK(occ.hasCandidates(1, 0));
    CHECK(occ.hasCandidates(2, 0));
    CHECK(occ.hasCandidates(3, 0));
    CHECK(countOccupied(occ, 4, 16) == 4);

    // No other shader group should be touched.
    CHECK_FALSE(occ.hasCandidates(0, SHADER_GROUP_TRANSPARENT));
    CHECK_FALSE(occ.hasCandidates(0, SHADER_GROUP_BLEND));
}

TEST_CASE("SectionOccupancy: distinct shader groups land in distinct sections")
{
    SectionOccupancy occ;
    occ.configure(4, 16);

    // Index 0 -> batch 0, group 0 (opaque)
    // Index 1 -> batch 1, group SHADER_GROUP_TRANSPARENT
    // Index 2 -> batch 2, group SHADER_GROUP_BLEND
    std::vector<GPUObjectData> objects{
        makeObject(0),
        makeObject(SHADER_GROUP_TRANSPARENT),
        makeObject(SHADER_GROUP_BLEND),
    };
    occ.recompute(objects, static_cast<uint32_t>(objects.size()));

    CHECK(occ.hasCandidates(0, 0));
    CHECK(occ.hasCandidates(1, SHADER_GROUP_TRANSPARENT));
    CHECK(occ.hasCandidates(2, SHADER_GROUP_BLEND));
    CHECK(countOccupied(occ, 4, 16) == 3);

    // Cross sections stay empty (e.g. batch 0 has no transparent/blend candidate).
    CHECK_FALSE(occ.hasCandidates(0, SHADER_GROUP_TRANSPARENT));
    CHECK_FALSE(occ.hasCandidates(0, SHADER_GROUP_BLEND));
    CHECK_FALSE(occ.hasCandidates(1, 0));
    CHECK_FALSE(occ.hasCandidates(2, 0));
}

TEST_CASE("SectionOccupancy: out-of-range shader group is ignored, not clamped")
{
    SectionOccupancy occ;
    occ.configure(4, 16);

    // Group == shaderGroupCount (16) and group well past it are both out of range.
    std::vector<GPUObjectData> objects{
        makeObject(MAX_SHADER_GROUPS),       // == shaderGroupCount, out of range
        makeObject(MAX_SHADER_GROUPS + 7),   // far out of range
    };
    occ.recompute(objects, static_cast<uint32_t>(objects.size()));

    // Nothing marked; importantly no clamping to group 0.
    CHECK(countOccupied(occ, 4, 16) == 0);
    CHECK_FALSE(occ.hasCandidates(0, 0));
}

TEST_CASE("SectionOccupancy: empty input occupies nothing")
{
    SectionOccupancy occ;
    occ.configure(4, 16);

    std::vector<GPUObjectData> objects;
    occ.recompute(objects, 0);
    CHECK(countOccupied(occ, 4, 16) == 0);

    // Non-empty buffer but count == 0 must also occupy nothing.
    objects.push_back(makeObject(0));
    occ.recompute(objects, 0);
    CHECK(countOccupied(occ, 4, 16) == 0);
}

TEST_CASE("SectionOccupancy: unconfigured grid occupies nothing")
{
    // Mirrors the init() guard: batchCount/shaderGroupCount default to 0 until configure().
    SectionOccupancy occ;  // never configured -> both counts 0
    std::vector<GPUObjectData> objects{ makeObject(0), makeObject(SHADER_GROUP_BLEND) };
    occ.recompute(objects, static_cast<uint32_t>(objects.size()));

    // recompute returns early; hasCandidates rejects every query (shaderGroupCount == 0).
    CHECK_FALSE(occ.hasCandidates(0, 0));
}

TEST_CASE("SectionOccupancy: count is clamped to vector size")
{
    SectionOccupancy occ;
    occ.configure(4, 16);

    // count claims more entries than exist; recompute must clamp to objects.size() and not
    // read out of bounds. Two opaque objects -> batches 0 and 1, group 0.
    std::vector<GPUObjectData> objects{ makeObject(0), makeObject(0) };
    occ.recompute(objects, 999);

    CHECK(occ.hasCandidates(0, 0));
    CHECK(occ.hasCandidates(1, 0));
    CHECK(countOccupied(occ, 4, 16) == 2);
}

TEST_CASE("SectionOccupancy: hasCandidates rejects out-of-range queries")
{
    SectionOccupancy occ;
    occ.configure(4, 16);
    std::vector<GPUObjectData> objects{ makeObject(0) };
    occ.recompute(objects, 1);

    CHECK(occ.hasCandidates(0, 0));
    CHECK_FALSE(occ.hasCandidates(4, 0));   // batch == batchCount, out of range
    CHECK_FALSE(occ.hasCandidates(0, 16));  // group == shaderGroupCount, out of range
    CHECK_FALSE(occ.hasCandidates(99, 99));
}

TEST_CASE("SectionOccupancy: recompute and clear reset prior state")
{
    SectionOccupancy occ;
    occ.configure(4, 16);

    std::vector<GPUObjectData> first{ makeObject(SHADER_GROUP_TRANSPARENT) };  // batch 0
    occ.recompute(first, 1);
    CHECK(occ.hasCandidates(0, SHADER_GROUP_TRANSPARENT));

    // A subsequent recompute with different contents must not retain the old section.
    std::vector<GPUObjectData> second{ makeObject(0) };  // batch 0, group 0
    occ.recompute(second, 1);
    CHECK(occ.hasCandidates(0, 0));
    CHECK_FALSE(occ.hasCandidates(0, SHADER_GROUP_TRANSPARENT));

    occ.clear();
    CHECK(countOccupied(occ, 4, 16) == 0);
}

TEST_CASE("SectionOccupancy: play-mode superset only over-marks, never under-marks")
{
    // Play mode iterates the whole persistent slot array (active + stale slots). Over-marking
    // stale slots is safe: a section that holds at least one real object must stay occupied,
    // and extra marks never hide geometry. Model this with a slot array where a real opaque
    // object sits at index 0 (batch 0, group 0) and a stale slot at index 4 (batch 0, group 0).
    SectionOccupancy editMode;
    editMode.configure(4, 16);
    std::vector<GPUObjectData> activeOnly{ makeObject(0) };  // edit mode: just the live object
    editMode.recompute(activeOnly, 1);

    SectionOccupancy playMode;
    playMode.configure(4, 16);
    std::vector<GPUObjectData> slots{
        makeObject(0),                       // index 0: live object, batch 0 group 0
        makeObject(0),                       // index 1: stale slot, batch 1 group 0 (over-mark)
        makeObject(SHADER_GROUP_TRANSPARENT) // index 2: stale slot, batch 2 group transparent
    };
    playMode.recompute(slots, static_cast<uint32_t>(slots.size()));

    // Superset property: every section occupied in edit mode is still occupied in play mode.
    for (uint32_t b = 0; b < 4; ++b)
        for (uint32_t g = 0; g < 16; ++g)
            if (editMode.hasCandidates(b, g))
                CHECK(playMode.hasCandidates(b, g));

    // Play mode may additionally light up stale-slot sections (over-marking) — that is allowed.
    CHECK(playMode.hasCandidates(1, 0));
    CHECK(playMode.hasCandidates(2, SHADER_GROUP_TRANSPARENT));
}
