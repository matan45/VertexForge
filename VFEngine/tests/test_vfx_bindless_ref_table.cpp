// CPU-only coverage for the VK-1481 VFX bindless refcount + deferred-teardown state
// machine in VFXBindlessRefTable.hpp. This is the tricky lifetime logic extracted out
// of the four VFX GPU pipelines' per-path TextureEntry caches so it can be exercised
// without a Vulkan device.
//
// Invariants under test:
//   * A fresh key needs registration; after setIndex it resolves to that slot at refCount 1.
//   * Repeat acquires of a live key dedup (share the slot) and bump refCount.
//   * release is balanced; the last release marks the key pending for teardown.
//   * Teardown is deferred FRAMES_BEFORE_DELETE (3) frames, using unsigned-wrap frame math.
//   * Re-acquiring a pending key cancels the scheduled teardown (texture stays resident).
//   * Double / absent release never underflows.
//   * A failed registration rolls the fresh key back cleanly.
//   * Srgb vs UNORM keys ("s:"/"l:" prefixed by the wrapper) are independent entries.
//
// Pure function of VFXBindlessRefTable — no render/GPU types, no Vulkan device.

#include <doctest.h>

#include <vfx/VFXBindlessRefTable.hpp>

#include <cstdint>

namespace
{
    constexpr uint32_t kFramesBeforeDelete = 3;
}

TEST_SUITE("VFXBindlessRefTable")
{
    TEST_CASE("acquire of a fresh key needs registration, setIndex records the slot")
    {
        vfx::VFXBindlessRefTable table(kFramesBeforeDelete);

        auto r = table.acquire("s:fire.vfImage");
        CHECK(r.needsRegister);
        CHECK(r.index == vfx::VFXBindlessRefTable::kInvalidIndex);
        // Before setIndex the slot is unknown.
        CHECK_FALSE(table.indexOf("s:fire.vfImage").has_value());

        table.setIndex("s:fire.vfImage", 7u);
        REQUIRE(table.indexOf("s:fire.vfImage").has_value());
        CHECK(table.indexOf("s:fire.vfImage").value() == 7u);
        CHECK(table.refCountOf("s:fire.vfImage") == 1u);
        CHECK(table.liveCount() == 1u);
    }

    TEST_CASE("second acquire dedups and bumps the refcount")
    {
        vfx::VFXBindlessRefTable table(kFramesBeforeDelete);
        REQUIRE(table.acquire("s:tex").needsRegister);
        table.setIndex("s:tex", 7u);

        auto r2 = table.acquire("s:tex");
        CHECK_FALSE(r2.needsRegister);
        CHECK(r2.index == 7u);
        CHECK(table.refCountOf("s:tex") == 2u);
        CHECK(table.liveCount() == 1u);
    }

    TEST_CASE("release is balanced; the last release marks the key pending")
    {
        vfx::VFXBindlessRefTable table(kFramesBeforeDelete);
        REQUIRE(table.acquire("s:tex").needsRegister);
        table.setIndex("s:tex", 7u);
        table.acquire("s:tex"); // refCount == 2

        table.release("s:tex", 100u);
        CHECK(table.refCountOf("s:tex") == 1u);
        CHECK_FALSE(table.isPending("s:tex"));

        table.release("s:tex", 100u);
        CHECK(table.refCountOf("s:tex") == 0u);
        CHECK(table.isPending("s:tex"));
    }

    TEST_CASE("teardown is deferred FRAMES_BEFORE_DELETE frames, then collectReady reports it")
    {
        vfx::VFXBindlessRefTable table(kFramesBeforeDelete);
        REQUIRE(table.acquire("s:tex").needsRegister);
        table.setIndex("s:tex", 7u);
        table.release("s:tex", 100u); // retiredFrame = 100

        CHECK(table.collectReady(101u).empty());
        CHECK(table.collectReady(102u).empty()); // 102 - 100 = 2 < 3

        auto ready = table.collectReady(103u); // 103 - 100 = 3 >= 3
        REQUIRE(ready.size() == 1u);
        CHECK(ready[0] == "s:tex");
        CHECK(table.indexOf("s:tex").value() == 7u); // slot still resolvable by key until forget()

        table.forget("s:tex");
        CHECK_FALSE(table.indexOf("s:tex").has_value());
        CHECK(table.liveCount() == 0u);
    }

    TEST_CASE("re-acquiring a pending key cancels the scheduled teardown")
    {
        vfx::VFXBindlessRefTable table(kFramesBeforeDelete);
        REQUIRE(table.acquire("s:tex").needsRegister);
        table.setIndex("s:tex", 7u);
        table.release("s:tex", 100u);
        REQUIRE(table.isPending("s:tex"));

        // Re-acquire before the window elapses: reuse the resident slot, no reload.
        auto r = table.acquire("s:tex");
        CHECK_FALSE(r.needsRegister);
        CHECK(r.index == 7u);
        CHECK_FALSE(table.isPending("s:tex"));
        CHECK(table.refCountOf("s:tex") == 1u);

        // The previously-scheduled teardown must no longer fire.
        CHECK(table.collectReady(103u).empty());
    }

    TEST_CASE("double / absent release never underflows")
    {
        vfx::VFXBindlessRefTable table(kFramesBeforeDelete);
        REQUIRE(table.acquire("s:tex").needsRegister);
        table.setIndex("s:tex", 7u);

        table.release("s:tex", 50u); // -> 0, pending
        table.release("s:tex", 60u); // extra release: no-op, no underflow
        CHECK(table.refCountOf("s:tex") == 0u);
        CHECK(table.isPending("s:tex"));

        table.release("s:absent", 60u); // release of a never-acquired key: no-op
        CHECK(table.liveCount() == 1u);
    }

    TEST_CASE("a failed registration rolls the fresh key back")
    {
        vfx::VFXBindlessRefTable table(kFramesBeforeDelete);
        auto r = table.acquire("s:broken");
        REQUIRE(r.needsRegister);

        table.fail("s:broken"); // load threw / table full / file missing
        CHECK_FALSE(table.indexOf("s:broken").has_value());
        CHECK(table.refCountOf("s:broken") == 0u);
        CHECK(table.liveCount() == 0u);

        // A later acquire of the same key is fresh again.
        CHECK(table.acquire("s:broken").needsRegister);
    }

    TEST_CASE("Srgb and UNORM keys for the same path are independent entries")
    {
        vfx::VFXBindlessRefTable table(kFramesBeforeDelete);

        auto s = table.acquire("s:shared.vfImage"); // billboard/mesh/ribbon (Srgb)
        REQUIRE(s.needsRegister);
        table.setIndex("s:shared.vfImage", 5u);

        auto l = table.acquire("l:shared.vfImage"); // distortion (UNORM)
        REQUIRE(l.needsRegister); // NOT deduped against the Srgb entry
        table.setIndex("l:shared.vfImage", 9u);

        CHECK(table.indexOf("s:shared.vfImage").value() == 5u);
        CHECK(table.indexOf("l:shared.vfImage").value() == 9u);
        CHECK(table.liveCount() == 2u);
    }

    TEST_CASE("teardown window works across an unsigned frame wrap")
    {
        vfx::VFXBindlessRefTable table(kFramesBeforeDelete);
        REQUIRE(table.acquire("s:tex").needsRegister);
        table.setIndex("s:tex", 7u);

        const uint32_t retire = 0xFFFFFFFFu - 1u; // retire near the wrap point
        table.release("s:tex", retire);

        CHECK(table.collectReady(0xFFFFFFFFu).empty()); // gap 1
        CHECK(table.collectReady(0u).empty());          // gap 2 (wrapped)

        auto ready = table.collectReady(1u); // gap 3 (wrapped): 1 - (2^32-2) == 3 mod 2^32
        REQUIRE(ready.size() == 1u);
        CHECK(ready[0] == "s:tex");
        CHECK(table.indexOf("s:tex").value() == 7u);
    }

    TEST_CASE("clear() drops all entries so the table can be reused")
    {
        vfx::VFXBindlessRefTable table(kFramesBeforeDelete);
        REQUIRE(table.acquire("s:a").needsRegister);
        table.setIndex("s:a", 3u);
        REQUIRE(table.acquire("l:b").needsRegister);
        table.setIndex("l:b", 4u);
        REQUIRE(table.liveCount() == 2u);

        table.clear();
        CHECK(table.liveCount() == 0u);
        CHECK_FALSE(table.indexOf("s:a").has_value());

        // A post-clear acquire of a previously-known key is treated as fresh again.
        CHECK(table.acquire("s:a").needsRegister);
    }
}
