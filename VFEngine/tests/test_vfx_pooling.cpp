// CPU-only coverage for the VK-1453 (Phase 4) dormant-instance reuse bookkeeping
// in VFXHandlePool.hpp:
//
//   * retain(id, path) then acquire(path) revives the same id.
//   * acquire on an empty pool or an unknown path returns 0.
//   * dormant instances are bucketed per asset path.
//   * exceeding maxDormant evicts the oldest id (returned by retain) and caps the
//     dormant count.
//   * forget(id) drops a dormant instance so it is no longer revived.
//   * clear() empties the pool.
//
// Pure bookkeeping — no renderer, no GPU slots, no Vulkan device.

#include <doctest.h>

#include <vfx/VFXHandlePool.hpp>

TEST_SUITE("VFXHandlePool")
{
    TEST_CASE("retain then acquire revives the same id")
    {
        vfx::VFXHandlePool pool(8);
        CHECK(pool.retain(42, "a.vfVFX") == 0); // nothing evicted
        CHECK(pool.dormantCount() == 1);

        CHECK(pool.acquire("a.vfVFX") == 42);
        CHECK(pool.dormantCount() == 0);
    }

    TEST_CASE("acquire on an empty pool or unknown path returns 0")
    {
        vfx::VFXHandlePool pool;
        CHECK(pool.acquire("missing.vfVFX") == 0);

        pool.retain(1, "known.vfVFX");
        CHECK(pool.acquire("other.vfVFX") == 0);   // wrong path
        CHECK(pool.acquire("known.vfVFX") == 1);   // right path
        CHECK(pool.acquire("known.vfVFX") == 0);   // bucket now empty
    }

    TEST_CASE("dormant instances are bucketed per path")
    {
        vfx::VFXHandlePool pool;
        pool.retain(10, "X");
        pool.retain(20, "Y");
        CHECK(pool.dormantCount() == 2);

        CHECK(pool.acquire("X") == 10);
        CHECK(pool.acquire("Y") == 20);
        CHECK(pool.dormantCount() == 0);
    }

    TEST_CASE("exceeding capacity evicts the oldest id and caps the dormant count")
    {
        vfx::VFXHandlePool pool(2);
        CHECK(pool.maxDormant() == 2);

        CHECK(pool.retain(1, "p") == 0);
        CHECK(pool.retain(2, "p") == 0);
        CHECK(pool.dormantCount() == 2);

        // Third retain evicts the oldest (id 1) and returns it.
        CHECK(pool.retain(3, "p") == 1);
        CHECK(pool.dormantCount() == 2); // still capped

        // Id 1 is gone; the two survivors are 2 and 3 (LIFO within the bucket).
        CHECK(pool.acquire("p") == 3);
        CHECK(pool.acquire("p") == 2);
        CHECK(pool.acquire("p") == 0);
    }

    TEST_CASE("a zero capacity is clamped to at least one slot")
    {
        vfx::VFXHandlePool pool(0);
        CHECK(pool.maxDormant() == 1);

        CHECK(pool.retain(1, "p") == 0);
        CHECK(pool.retain(2, "p") == 1); // first slot evicted
        CHECK(pool.dormantCount() == 1);
        CHECK(pool.acquire("p") == 2);
    }

    TEST_CASE("forget drops a dormant id so it is not revived")
    {
        vfx::VFXHandlePool pool;
        pool.retain(5, "p");
        pool.retain(6, "p");

        pool.forget(5);
        CHECK(pool.dormantCount() == 1);

        // Only 6 remains.
        CHECK(pool.acquire("p") == 6);
        CHECK(pool.acquire("p") == 0);
    }

    TEST_CASE("forget of an unknown id is a no-op")
    {
        vfx::VFXHandlePool pool;
        pool.retain(7, "p");
        pool.forget(999); // not present
        CHECK(pool.dormantCount() == 1);
        CHECK(pool.acquire("p") == 7);
    }

    TEST_CASE("clear empties the pool")
    {
        vfx::VFXHandlePool pool;
        pool.retain(1, "a");
        pool.retain(2, "b");
        REQUIRE(pool.dormantCount() == 2);

        pool.clear();
        CHECK(pool.dormantCount() == 0);
        CHECK(pool.acquire("a") == 0);
        CHECK(pool.acquire("b") == 0);
    }
}
