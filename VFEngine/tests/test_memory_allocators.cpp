#include <doctest.h>
#include <memory/LinearAllocator.hpp>
#include <memory/PoolAllocator.hpp>
#include <memory/FreeListHeapAllocator.hpp>
#include <memory/FrameAllocator.hpp>
#include <memory/TypedPoolAllocator.hpp>
#include <memory/MemoryTypes.hpp>
#ifdef DEBUG
#include <memory/MemoryDebugMacros.hpp>
#endif
#include <thread>
#include <vector>
#include <atomic>

// ============================================================
// VK-1081: Memory Management Allocator unit tests
// ============================================================

TEST_SUITE("MemoryAllocators") {

// ---- AllocationHandle ----

TEST_CASE("AllocationHandle: default is invalid") {
    memory::AllocationHandle handle;
    CHECK_FALSE(handle.isValid());
}

TEST_CASE("AllocationHandle: valid handle") {
    memory::AllocationHandle handle;
    handle.offset = 0;
    handle.size = 64;
    CHECK(handle.isValid());
}

TEST_CASE("AllocationHandle: equality") {
    memory::AllocationHandle a{0, 64, 1};
    memory::AllocationHandle b{0, 64, 1};
    memory::AllocationHandle c{8, 64, 1};
    CHECK(a == b);
    CHECK(a != c);
}

// ---- LinearAllocator ----

TEST_CASE("LinearAllocator: allocate returns valid handle") {
    memory::LinearAllocator alloc(1024, "TestLinear");
    auto handle = alloc.allocate(64);
    CHECK(handle.isValid());
    CHECK(handle.size == 64);
}

TEST_CASE("LinearAllocator: sequential allocations have increasing offsets") {
    memory::LinearAllocator alloc(1024, "TestLinear");
    auto h1 = alloc.allocate(64);
    auto h2 = alloc.allocate(64);
    CHECK(h2.offset > h1.offset);
}

TEST_CASE("LinearAllocator: alignment is respected") {
    memory::LinearAllocator alloc(1024, "TestLinear");
    auto h1 = alloc.allocate(1, 1);   // 1-byte alloc
    auto h2 = alloc.allocate(64, 16); // 16-byte aligned
    CHECK((h2.offset % 16) == 0);
}

TEST_CASE("LinearAllocator: various alignments") {
    memory::LinearAllocator alloc(4096, "TestLinear");
    for (uint64_t align : {1, 4, 8, 16, 64, 256}) {
        auto handle = alloc.allocate(32, align);
        CHECK(handle.isValid());
        CHECK((handle.offset % align) == 0);
    }
}

TEST_CASE("LinearAllocator: allocate until full") {
    memory::LinearAllocator alloc(128, "TestLinear");
    auto h1 = alloc.allocate(128);
    CHECK(h1.isValid());
    auto h2 = alloc.allocate(1);
    CHECK_FALSE(h2.isValid());
}

TEST_CASE("LinearAllocator: reset reclaims space") {
    memory::LinearAllocator alloc(128, "TestLinear");
    alloc.allocate(128);
    alloc.reset();
    auto handle = alloc.allocate(128);
    CHECK(handle.isValid());
}

TEST_CASE("LinearAllocator: getPointer returns non-null") {
    memory::LinearAllocator alloc(1024, "TestLinear");
    auto handle = alloc.allocate(64);
    void* ptr = alloc.getPointer(handle);
    CHECK(ptr != nullptr);
}

TEST_CASE("LinearAllocator: stats tracking") {
    memory::LinearAllocator alloc(1024, "TestLinear");
    alloc.allocate(100);
    alloc.allocate(200);
    auto stats = alloc.getStats();
    CHECK(stats.allocationCount == 2);
    CHECK(stats.totalCapacity == 1024);
}

TEST_CASE("LinearAllocator: strategy is Linear") {
    memory::LinearAllocator alloc(1024, "TestLinear");
    CHECK(alloc.getStrategy() == memory::AllocatorStrategy::Linear);
}

TEST_CASE("LinearAllocator: concurrent allocations") {
    memory::LinearAllocator alloc(1024 * 1024, "TestLinearConcurrent");
    std::atomic<int> successCount{0};
    constexpr int threadCount = 8;
    constexpr int allocsPerThread = 100;

    std::vector<std::thread> threads;
    for (int t = 0; t < threadCount; ++t) {
        threads.emplace_back([&]() {
            for (int i = 0; i < allocsPerThread; ++i) {
                auto handle = alloc.allocate(64);
                if (handle.isValid()) successCount++;
            }
        });
    }
    for (auto& t : threads) t.join();

    CHECK(successCount == threadCount * allocsPerThread);
}

// ---- PoolAllocator ----

TEST_CASE("PoolAllocator: allocate and free") {
    memory::PoolAllocator pool(64, 10, "TestPool");
    auto handle = pool.allocate(64);
    CHECK(handle.isValid());
    pool.free(handle);
}

TEST_CASE("PoolAllocator: rejects oversized allocation") {
    memory::PoolAllocator pool(64, 10, "TestPool");
    auto handle = pool.allocate(128);
    CHECK_FALSE(handle.isValid());
}

TEST_CASE("PoolAllocator: allocate all blocks") {
    constexpr uint32_t count = 5;
    memory::PoolAllocator pool(64, count, "TestPool");
    std::vector<memory::AllocationHandle> handles;
    for (uint32_t i = 0; i < count; ++i) {
        auto h = pool.allocate(64);
        CHECK(h.isValid());
        handles.push_back(h);
    }
    // Pool exhausted
    auto overflow = pool.allocate(64);
    CHECK_FALSE(overflow.isValid());
}

TEST_CASE("PoolAllocator: free and reuse") {
    memory::PoolAllocator pool(64, 2, "TestPool");
    auto h1 = pool.allocate(64);
    auto h2 = pool.allocate(64);
    CHECK_FALSE(pool.allocate(64).isValid()); // exhausted

    pool.free(h1);
    auto h3 = pool.allocate(64);
    CHECK(h3.isValid()); // reused freed block
}

TEST_CASE("PoolAllocator: full cycle - free all then allocate all") {
    constexpr uint32_t count = 10;
    memory::PoolAllocator pool(64, count, "TestPool");
    std::vector<memory::AllocationHandle> handles;

    // Allocate all
    for (uint32_t i = 0; i < count; ++i)
        handles.push_back(pool.allocate(64));

    // Free all
    for (auto& h : handles) pool.free(h);
    handles.clear();

    // Allocate all again
    for (uint32_t i = 0; i < count; ++i) {
        auto h = pool.allocate(64);
        CHECK(h.isValid());
        handles.push_back(h);
    }
}

TEST_CASE("PoolAllocator: strategy is Pool") {
    memory::PoolAllocator pool(64, 10, "TestPool");
    CHECK(pool.getStrategy() == memory::AllocatorStrategy::Pool);
}

TEST_CASE("PoolAllocator: concurrent alloc/free") {
    memory::PoolAllocator pool(64, 1000, "TestPoolConcurrent");
    std::atomic<int> successCount{0};
    constexpr int threadCount = 4;
    constexpr int opsPerThread = 100;

    std::vector<std::thread> threads;
    for (int t = 0; t < threadCount; ++t) {
        threads.emplace_back([&]() {
            for (int i = 0; i < opsPerThread; ++i) {
                auto handle = pool.allocate(64);
                if (handle.isValid()) {
                    successCount++;
                    pool.free(handle);
                }
            }
        });
    }
    for (auto& t : threads) t.join();

    CHECK(successCount == threadCount * opsPerThread);
}

// ---- FreeListHeapAllocator ----

TEST_CASE("FreeListHeapAllocator: allocate varying sizes") {
    memory::FreeListHeapAllocator heap(4096, "TestHeap", true);
    auto h1 = heap.allocate(32);
    auto h2 = heap.allocate(128);
    auto h3 = heap.allocate(256);
    CHECK(h1.isValid());
    CHECK(h2.isValid());
    CHECK(h3.isValid());
}

TEST_CASE("FreeListHeapAllocator: alignment") {
    memory::FreeListHeapAllocator heap(4096, "TestHeap", true);
    auto h = heap.allocate(64, 256);
    CHECK(h.isValid());
    CHECK((h.offset % 256) == 0);
}

TEST_CASE("FreeListHeapAllocator: free and coalescing") {
    memory::FreeListHeapAllocator heap(512, "TestHeap", true);

    // Allocate three adjacent blocks
    auto h1 = heap.allocate(100);
    auto h2 = heap.allocate(100);
    auto h3 = heap.allocate(100);

    // Free middle then neighbors — should coalesce
    heap.free(h2);
    heap.free(h1);
    heap.free(h3);

    // Should be able to allocate a large block from coalesced space
    auto big = heap.allocate(300);
    CHECK(big.isValid());
}

TEST_CASE("FreeListHeapAllocator: allocate-free-allocate reuses space") {
    memory::FreeListHeapAllocator heap(256, "TestHeap", true);
    auto h1 = heap.allocate(128);
    heap.free(h1);
    auto h2 = heap.allocate(128);
    CHECK(h2.isValid());
}

TEST_CASE("FreeListHeapAllocator: fragmentation reporting") {
    memory::FreeListHeapAllocator heap(1024, "TestHeap", true);
    auto stats = heap.getStats();
    CHECK(stats.fragmentationPercent >= 0.0f);
    CHECK(stats.fragmentationPercent <= 100.0f);
}

TEST_CASE("FreeListHeapAllocator: strategy is FreeList") {
    memory::FreeListHeapAllocator heap(1024, "TestHeap");
    CHECK(heap.getStrategy() == memory::AllocatorStrategy::FreeList);
}

TEST_CASE("FreeListHeapAllocator: reset reclaims all") {
    memory::FreeListHeapAllocator heap(1024, "TestHeap", true);
    heap.allocate(512);
    heap.allocate(512);
    heap.reset();
    auto h = heap.allocate(1024);
    CHECK(h.isValid());
}

TEST_CASE("FreeListHeapAllocator: concurrent alloc/free") {
    memory::FreeListHeapAllocator heap(1024 * 1024, "TestHeapConcurrent", true);
    std::atomic<int> successCount{0};
    constexpr int threadCount = 4;
    constexpr int opsPerThread = 100;

    std::vector<std::thread> threads;
    for (int t = 0; t < threadCount; ++t) {
        threads.emplace_back([&]() {
            for (int i = 0; i < opsPerThread; ++i) {
                auto handle = heap.allocate(64);
                if (handle.isValid()) {
                    successCount++;
                    heap.free(handle);
                }
            }
        });
    }
    for (auto& t : threads) t.join();

    CHECK(successCount == threadCount * opsPerThread);
}

// ---- FrameAllocator ----

TEST_CASE("FrameAllocator: allocate in frame 0") {
    memory::FrameAllocator frame(1024, "TestFrame");
    frame.setFrame(0);
    auto handle = frame.allocate(64);
    CHECK(handle.isValid());
}

TEST_CASE("FrameAllocator: double-buffered frame isolation") {
    memory::FrameAllocator frame(256, "TestFrame");

    // Frame 0: allocate some data
    frame.setFrame(0);
    auto h0 = frame.allocate(128);
    CHECK(h0.isValid());
    void* ptr0 = frame.getPointer(h0);
    CHECK(ptr0 != nullptr);

    // Frame 1: allocate independently
    frame.setFrame(1);
    auto h1 = frame.allocate(128);
    CHECK(h1.isValid());

    // Frame 0 again: previous allocations are reset
    frame.setFrame(0);
    auto h0b = frame.allocate(256); // full budget available
    CHECK(h0b.isValid());
}

TEST_CASE("FrameAllocator: getCurrentFrame") {
    memory::FrameAllocator frame(256, "TestFrame");
    frame.setFrame(0);
    CHECK(frame.getCurrentFrame() == 0);
    frame.setFrame(1);
    CHECK(frame.getCurrentFrame() == 1);
}

// ---- TypedPoolAllocator ----

TEST_CASE("TypedPoolAllocator: allocate and free") {
    struct TestObj {
        int x = 42;
        float y = 3.14f;
    };

    memory::TypedPoolAllocator<TestObj> pool(10, "TestTypedPool");
    TestObj* obj = pool.allocate();
    CHECK(obj != nullptr);
    CHECK(obj->x == 42);
    CHECK(obj->y == doctest::Approx(3.14f));
    pool.free(obj);
}

TEST_CASE("TypedPoolAllocator: stats") {
    memory::TypedPoolAllocator<int> pool(5, "TestTypedPool");
    pool.allocate();
    pool.allocate();
    auto stats = pool.getStats();
    CHECK(stats.allocationCount >= 2);
}

// ---- Debug Tracking (DEBUG only) ----

#ifdef DEBUG
TEST_CASE("DebugAllocatorWrapper: track allocation with file/line") {
    memory::LinearAllocator alloc(1024, "TestDebug");
    auto handle = memory::debugAllocate(alloc, 64, 1, __FILE__, __LINE__);
    CHECK(handle.isValid());
    memory::debugFree(alloc, handle, __FILE__, __LINE__);
}
#endif

} // TEST_SUITE
