#include <doctest.h>

#include <vfx/VFXChildSpawn.hpp>
#include <vfx/VFXEventTypes.hpp>

#include <set>

// VK-1501 -- CPU-only tests for the GPU event->child fast path. The graphics side (ping-pong ring,
// sentinel gating, barriers) is GPU-only and verified in-engine; these cover the pure policy that
// the renderer and shader both depend on: region bookkeeping, eventChildSlot packing, the buffer
// addressing formula, frame parity, and the gpuFastPath authoring round-trip.

namespace
{
    using namespace vfx::child;
}

TEST_SUITE("VFXGpuChildSpawn")
{
    TEST_CASE("region allocator dedups by path and hands out distinct regions")
    {
        VFXChildRegionAllocator alloc;

        auto a = alloc.acquire("spark.vfVFX");
        auto b = alloc.acquire("debris.vfVFX");
        REQUIRE(a.has_value());
        REQUIRE(b.has_value());
        CHECK(*a != *b);
        CHECK(alloc.activeRegions() == 2);

        // Same path -> same region (shared, refcounted), no new region consumed.
        auto aAgain = alloc.acquire("spark.vfVFX");
        REQUIRE(aAgain.has_value());
        CHECK(*aAgain == *a);
        CHECK(alloc.activeRegions() == 2);

        CHECK(alloc.regionOfPath("spark.vfVFX").value() == *a);
        CHECK(!alloc.regionOfPath("missing.vfVFX").has_value());
    }

    TEST_CASE("region is freed only when the last reference is released, then reused")
    {
        VFXChildRegionAllocator alloc;
        auto first = alloc.acquire("a.vfVFX"); // refcount 1
        alloc.acquire("a.vfVFX");              // refcount 2
        REQUIRE(first.has_value());

        alloc.release("a.vfVFX"); // refcount 1 -> still held
        CHECK(alloc.regionOfPath("a.vfVFX").value() == *first);
        CHECK(alloc.activeRegions() == 1);

        alloc.release("a.vfVFX"); // refcount 0 -> freed
        CHECK(!alloc.regionOfPath("a.vfVFX").has_value());
        CHECK(alloc.activeRegions() == 0);

        // The freed index is recycled by the next distinct path.
        auto reused = alloc.acquire("b.vfVFX");
        REQUIRE(reused.has_value());
        CHECK(*reused == *first);

        // Releasing an unknown path is a no-op.
        alloc.release("never-acquired.vfVFX");
        CHECK(alloc.activeRegions() == 1);
    }

    TEST_CASE("region allocator exhausts gracefully at CHILD_MAX_REGIONS")
    {
        VFXChildRegionAllocator alloc;
        std::set<uint32_t> regions;
        for (uint32_t i = 0; i < CHILD_MAX_REGIONS; ++i)
        {
            auto r = alloc.acquire("path_" + std::to_string(i) + ".vfVFX");
            REQUIRE(r.has_value());
            CHECK(*r < CHILD_MAX_REGIONS);
            regions.insert(*r);
        }
        CHECK(regions.size() == CHILD_MAX_REGIONS); // all distinct

        // One past capacity -> nullopt (caller falls back to the CPU readback path).
        CHECK(!alloc.acquire("overflow.vfVFX").has_value());

        // Freeing one makes exactly one slot available again.
        alloc.release("path_0.vfVFX");
        CHECK(alloc.acquire("overflow.vfVFX").has_value());
        CHECK(!alloc.acquire("overflow2.vfVFX").has_value());

        alloc.reset();
        CHECK(alloc.activeRegions() == 0);
        CHECK(alloc.acquire("fresh.vfVFX").has_value());
    }

    TEST_CASE("eventChildSlot packs two independent halves with inherit bits")
    {
        // OnDeath: region 5, inherit color+size (not velocity). OnCollision: region 12, inherit vel.
        const uint32_t deathHalf = packHalf(5, /*color*/ true, /*size*/ true, /*vel*/ false);
        const uint32_t collisionHalf = packHalf(12, /*color*/ false, /*size*/ false, /*vel*/ true);
        const uint32_t slot = packSlot(deathHalf, collisionHalf);

        const uint32_t d = halfOf(slot, /*collision*/ false);
        const uint32_t c = halfOf(slot, /*collision*/ true);

        CHECK(hasRegion(d));
        CHECK(regionOf(d) == 5);
        CHECK(inheritColorOf(d));
        CHECK(inheritSizeOf(d));
        CHECK(!inheritVelOf(d));

        CHECK(hasRegion(c));
        CHECK(regionOf(c) == 12);
        CHECK(!inheritColorOf(c));
        CHECK(!inheritSizeOf(c));
        CHECK(inheritVelOf(c));
    }

    TEST_CASE("sentinel halves report no region and PACKED_NONE round-trips")
    {
        CHECK(!hasRegion(noneHalf()));
        CHECK(packSlot(noneHalf(), noneHalf()) == PACKED_NONE);

        // Default GPUEmitterConfig::eventChildSlot is 0xFFFFFFFF -> both halves are "no child".
        CHECK(!hasRegion(halfOf(0xFFFFFFFFu, false)));
        CHECK(!hasRegion(halfOf(0xFFFFFFFFu, true)));

        // Only one event fast-pathed: the other half stays a sentinel.
        const uint32_t deathOnly = packSlot(packHalf(3, true, true, true), noneHalf());
        CHECK(hasRegion(halfOf(deathOnly, false)));
        CHECK(regionOf(halfOf(deathOnly, false)) == 3);
        CHECK(!hasRegion(halfOf(deathOnly, true)));
    }

    TEST_CASE("region 0 is a valid region, distinct from the sentinel")
    {
        // 0xFF (255) is the sentinel; region 0 must not read as "no child".
        const uint32_t half = packHalf(0, false, false, false);
        CHECK(hasRegion(half));
        CHECK(regionOf(half) == 0);
    }

    TEST_CASE("child ring addressing is stable and collision-free")
    {
        // Mirror of the shader's index math: counters live at [half*R + region]; a request lives at
        // [(half*R + region)*K + slot]. Verify the two never alias across halves/regions/slots.
        const uint32_t R = CHILD_MAX_REGIONS;
        const uint32_t K = CHILD_MAX_REQUESTS_PER_REGION;
        std::set<uint32_t> dataIndices;
        for (uint32_t half = 0; half < 2; ++half)
        {
            for (uint32_t region : {0u, 1u, R - 1u})
            {
                const uint32_t base = half * R + region;
                CHECK(base < 2u * R); // counter index in range
                for (uint32_t slot : {0u, 1u, K - 1u})
                {
                    const uint32_t dataIndex = base * K + slot;
                    CHECK(dataIndex < 2u * R * K); // request index in range
                    CHECK(dataIndices.insert(dataIndex).second); // unique
                }
            }
        }
    }

    TEST_CASE("frame parity keeps the write and read halves disjoint")
    {
        // The shader picks writeHalf = frameNumber & 1 (parents append) and readHalf = writeHalf ^ 1
        // (child consumes last frame). They must always differ so there is no read-while-write race.
        for (uint32_t frame = 0; frame < 8; ++frame)
        {
            const uint32_t writeHalf = frame & 1u;
            const uint32_t readHalf = writeHalf ^ 1u;
            CHECK(writeHalf != readHalf);
            CHECK((writeHalf | readHalf) == 1u);
            // This frame's read half is exactly the previous frame's write half.
            CHECK(readHalf == ((frame + 1u) & 1u));
        }
    }

    TEST_CASE("gpuFastPath authoring round-trips through a VFX node")
    {
        vfx::VFXEventConfig config;
        auto& death = config.types[static_cast<size_t>(vfx::VFXEventType::OnDeath)];
        death.enabled = true;
        death.vfxPath = "spark.vfVFX";
        death.gpuFastPath = true;
        death.inheritColor = true;

        vfx::VFXNode node;
        vfx::storeEventConfigToNode(node, config);
        const auto loaded = vfx::loadEventConfigFromNode(node);

        const auto& d = loaded.types[static_cast<size_t>(vfx::VFXEventType::OnDeath)];
        CHECK(d.gpuFastPath);
        CHECK(d.inheritColor);

        // Default (unset) events keep gpuFastPath false so opt-out reproduces the CPU path.
        const auto& spawn = loaded.types[static_cast<size_t>(vfx::VFXEventType::OnSpawn)];
        CHECK(spawn.gpuFastPath == vfx::EventDefaults::GPU_FAST_PATH);
        CHECK(spawn.gpuFastPath == false);
    }
}
