#include <doctest.h>
#include <resource/AssetLifecycleManager.hpp>
#include <asset/AssetGUID.hpp>
#include <vector>

// ============================================================
// AssetLifecycleManager memory budget: pressure-accelerated
// release of unreferenced assets. The manager is a process-wide
// singleton, so each case clears it and resets the budget.
// ============================================================

namespace
{
    constexpr size_t MB = 1024 * 1024;

    struct LifecycleFixture
    {
        resource::AssetLifecycleManager& lifecycle = resource::AssetLifecycleManager::instance();
        std::vector<asset::AssetGUID> released;

        LifecycleFixture()
        {
            lifecycle.clear();
            lifecycle.setMemoryBudget({});
            lifecycle.setReleaseCallback(
                [this](const asset::AssetGUID& guid, resource::AssetType)
                {
                    released.push_back(guid);
                });
        }

        ~LifecycleFixture()
        {
            lifecycle.setReleaseCallback(nullptr);
            lifecycle.setMemoryBudget({});
            lifecycle.clear();
        }

        // Acquire + release: entry lands in PendingRelease with full grace
        asset::AssetGUID addUnreferenced(size_t bytes)
        {
            auto guid = asset::AssetGUID::generate();
            lifecycle.acquire(guid, resource::AssetType::Texture, bytes);
            lifecycle.release(guid);
            return guid;
        }
    };
}

TEST_SUITE("AssetLifecycleMemoryBudget")
{

TEST_CASE("under budget the grace period is respected")
{
    LifecycleFixture fix;
    fix.lifecycle.setMemoryBudget({100 * MB, 64});

    auto guid = fix.addUnreferenced(1 * MB);
    fix.lifecycle.tick(1.0f);

    CHECK(fix.released.empty());
    CHECK(fix.lifecycle.isTracked(guid));
    CHECK_FALSE(fix.lifecycle.isOverBudget());

    // Grace runs out naturally (5s default)
    for (int i = 0; i < 6; ++i) fix.lifecycle.tick(1.0f);
    CHECK(fix.released.size() == 1);
    CHECK_FALSE(fix.lifecycle.isTracked(guid));
}

TEST_CASE("over budget pending releases skip the grace period")
{
    LifecycleFixture fix;
    fix.lifecycle.setMemoryBudget({1, 64}); // 1 byte: everything is over budget

    auto guid = fix.addUnreferenced(1 * MB);
    CHECK(fix.lifecycle.isOverBudget());

    fix.lifecycle.tick(0.016f);

    CHECK(fix.released.size() == 1);
    CHECK(fix.released[0] == guid);
    CHECK_FALSE(fix.lifecycle.isTracked(guid));
}

TEST_CASE("referenced assets are never evicted by budget pressure")
{
    LifecycleFixture fix;
    fix.lifecycle.setMemoryBudget({1, 64});

    auto referenced = asset::AssetGUID::generate();
    fix.lifecycle.acquire(referenced, resource::AssetType::Mesh, 10 * MB);

    auto unreferenced = fix.addUnreferenced(1 * MB);

    for (int i = 0; i < 10; ++i) fix.lifecycle.tick(0.016f);

    CHECK(fix.lifecycle.isTracked(referenced));
    CHECK(fix.lifecycle.getAssetEntry(referenced).refCount == 1);
    CHECK_FALSE(fix.lifecycle.isTracked(unreferenced));
    // Still over budget — referenced bytes can't be freed, only reported
    CHECK(fix.lifecycle.isOverBudget());
}

TEST_CASE("pressure releasing stops once the projected total is under budget")
{
    LifecycleFixture fix;
    // 3 x 1MB unreferenced; budget allows 2.5MB
    fix.lifecycle.setMemoryBudget({(5 * MB) / 2, 64});

    fix.addUnreferenced(1 * MB);
    fix.addUnreferenced(1 * MB);
    fix.addUnreferenced(1 * MB);

    fix.lifecycle.tick(0.016f);

    // Only the oldest had to go: 3MB -> 2MB <= 2.5MB
    CHECK(fix.released.size() == 1);
    CHECK(fix.lifecycle.getTotalTrackedBytes() == 2 * MB);
    CHECK_FALSE(fix.lifecycle.isOverBudget());
}

TEST_CASE("pressure release respects its own per-frame throttle")
{
    LifecycleFixture fix;
    fix.lifecycle.setMemoryBudget({1, 4}); // throttle: 4 releases per tick

    for (int i = 0; i < 10; ++i) fix.addUnreferenced(1 * MB);

    fix.lifecycle.tick(0.016f);
    CHECK(fix.released.size() == 4);

    fix.lifecycle.tick(0.016f);
    CHECK(fix.released.size() == 8);

    fix.lifecycle.tick(0.016f);
    CHECK(fix.released.size() == 10);
}

TEST_CASE("getTotalTrackedBytes sums all tracked entries")
{
    LifecycleFixture fix;

    auto a = asset::AssetGUID::generate();
    auto b = asset::AssetGUID::generate();
    fix.lifecycle.acquire(a, resource::AssetType::Texture, 3 * MB);
    fix.lifecycle.acquire(b, resource::AssetType::Audio, 2 * MB);

    CHECK(fix.lifecycle.getTotalTrackedBytes() == 5 * MB);
    CHECK_FALSE(fix.lifecycle.isOverBudget()); // budget disabled by default
}

}
