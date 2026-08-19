#include <doctest.h>
#include <resource/ResourceLoadScheduler.hpp>
#include <resource/ResourceLoadTypes.hpp>
#include <resource/AssetTypes.hpp>
#include <asset/AssetGUID.hpp>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

// ============================================================
// VK-1592: sector and HLOD file IO submitted to the shared
// ResourceLoadScheduler instead of bare std::async.
//
// The scheduler is a process-wide singleton and the JobSystem is
// uninitialized in tests, so dispatched loads execute
// synchronously inside submit()/update(). Cases that shrink the
// concurrency cap must restore it, and look their own request up
// by requestId rather than assuming an empty ring.
// ============================================================

namespace
{
    using resource::LoadImportance;
    using resource::LoadHint;
    using resource::LoadStage;

    // These mirror the constants in WorldSectorStreamingOps.cpp. They are duplicated rather
    // than shared because the streamer lives behind WorldSectorServiceImpl, which needs a whole
    // engine to construct - the contract worth pinning is the resulting ORDERING, which is a
    // property of ResourceLoadScheduler::computePriority alone.
    constexpr float kSectorActivateBias = 1.5f;
    constexpr float kHlodProxyBias = 0.5f;
    constexpr float kSectorPrefetchBias = 0.0f;

    LoadHint hintAt(float distance, LoadImportance importance, float bias)
    {
        LoadHint hint;
        hint.worldPosition = glm::vec3(distance, 0.0f, 0.0f);
        hint.importance = importance;
        hint.priority = bias;
        return hint;
    }

    LoadHint activateHint(float distance)
    {
        return hintAt(distance, LoadImportance::High, kSectorActivateBias);
    }

    LoadHint prefetchHint(float distance)
    {
        return hintAt(distance, LoadImportance::Background, kSectorPrefetchBias);
    }

    LoadHint hlodHint(float distance)
    {
        return hintAt(distance, LoadImportance::Low, kHlodProxyBias);
    }

    float scoreOf(const LoadHint& hint)
    {
        return resource::ResourceLoadScheduler::computePriority(hint, glm::vec3(0.0f));
    }

    resource::CompletedLoadRecord findRecord(uint64_t requestId)
    {
        auto records = resource::ResourceLoadScheduler::instance().getRecentCompletions();
        auto it = std::find_if(records.begin(), records.end(),
            [&](const auto& r) { return r.requestId == requestId; });
        REQUIRE(it != records.end());
        return *it;
    }

    // Distances spanning a plausible activate ring: 4 sectors of 256 world units is ~1000, and
    // a multi-source or teleport case reaches much further.
    const std::vector<float> kDistances = {0.0f, 100.0f, 512.0f, 1024.0f, 5000.0f, 50000.0f};
}

TEST_SUITE("SectorLoadScheduling")
{

TEST_CASE("every existing asset load scores a flat 1.5")
{
    // The whole priority map is built on this: ResourceManager passes a default LoadHint for
    // textures, meshes, audio, animations and fonts, and none of them carry a world position.
    // If this ever changes, the biases below have to be re-derived.
    CHECK(scoreOf(LoadHint{}) == doctest::Approx(1.5f));
}

TEST_CASE("activate-ring sectors outrank every default asset load, at any distance")
{
    for (float distance : kDistances)
    {
        CAPTURE(distance);
        CHECK(scoreOf(activateHint(distance)) > scoreOf(LoadHint{}));
    }
}

TEST_CASE("speculative sector prefetch never displaces a real asset load")
{
    for (float distance : kDistances)
    {
        CAPTURE(distance);
        CHECK(scoreOf(prefetchHint(distance)) < scoreOf(LoadHint{}));
    }
}

TEST_CASE("HLOD proxies sit between asset loads and sector prefetch")
{
    for (float distance : kDistances)
    {
        CAPTURE(distance);
        CHECK(scoreOf(hlodHint(distance)) < scoreOf(LoadHint{}));
        // A proxy at the far edge of its tier still beats a prefetch standing on the camera.
        CHECK(scoreOf(hlodHint(distance)) > scoreOf(prefetchHint(0.0f)));
    }
}

TEST_CASE("distance orders sectors within a ring")
{
    CHECK(scoreOf(activateHint(0.0f)) > scoreOf(activateHint(512.0f)));
    CHECK(scoreOf(activateHint(512.0f)) > scoreOf(activateHint(4096.0f)));

    CHECK(scoreOf(prefetchHint(0.0f)) > scoreOf(prefetchHint(512.0f)));
    CHECK(scoreOf(prefetchHint(512.0f)) > scoreOf(prefetchHint(4096.0f)));
}

TEST_CASE("a near sector is dispatched ahead of a far one and of a plain asset load")
{
    auto& scheduler = resource::ResourceLoadScheduler::instance();

    // Zero slots first, so submit()'s eager dispatch cannot place anything and all three
    // requests reach the heap together. cancelAll makes the single slot below deterministic
    // against anything an earlier case left pending in the shared singleton.
    resource::ResourceSchedulerConfig hold;
    hold.maxConcurrentLoads = 0;
    scheduler.init(hold);
    // Pin the scheduler's camera to the origin: computePriority is distance-relative and the
    // singleton remembers whatever position the previous case passed.
    scheduler.update(glm::vec3(0.0f));
    scheduler.cancelAll();

    // shared_ptr, not a captured reference: an executeLoad lambda that never runs stays alive
    // inside the process-wide scheduler, and a stack-captured vector would dangle into whichever
    // later case next pumps update().
    auto order = std::make_shared<std::vector<std::string>>();

    auto submitNamed = [&](const char* label, const LoadHint& hint)
    {
        resource::LoadRequest request;
        request.guid = asset::AssetGUID::generate();
        request.hint = hint;
        request.debugName = label;
        request.executeLoad = [order, label]() { order->push_back(label); };
        scheduler.submit(request);
    };

    submitNamed("farSector", activateHint(8000.0f));
    submitNamed("texture", LoadHint{});
    submitNamed("nearSector", activateHint(64.0f));

    // One slot at a time, so each update() dispatches exactly the current heap top.
    resource::ResourceSchedulerConfig oneSlot;
    oneSlot.maxConcurrentLoads = 1;
    scheduler.init(oneSlot);
    for (int i = 0; i < 4; ++i)
        scheduler.update(glm::vec3(0.0f));

    REQUIRE(order->size() == 3);
    CHECK((*order)[0] == "nearSector");
    CHECK((*order)[1] == "farSector"); // still above the flat-1.5 asset population
    CHECK((*order)[2] == "texture");

    scheduler.init({});
}

TEST_CASE("a sector request is visible in the profiler snapshot with its type and name")
{
    auto& scheduler = resource::ResourceLoadScheduler::instance();

    // Zero slots keep it pending so getActiveLoads can observe it
    resource::ResourceSchedulerConfig config;
    config.maxConcurrentLoads = 0;
    scheduler.init(config);
    scheduler.update(glm::vec3(0.0f)); // pin the camera to the origin (see above)

    auto guid = asset::AssetGUID::fromValue(0xFFF5'0000'0000'1234ull);

    resource::LoadRequest request;
    request.guid = guid;
    request.hint = activateHint(128.0f);
    request.hint.sectorId = 0x1234;
    request.assetType = resource::AssetType::WorldSector;
    request.debugName = "sector(3,-2)";
    // estimatedBytes deliberately left at 0 (ungated): test_cpu_memory.cpp drives the shared
    // CpuMemoryManager budget, and a gated load could be deferred by whatever it left behind.
    request.executeLoad = []() {};
    uint64_t id = scheduler.submit(request);

    auto active = scheduler.getActiveLoads();
    auto it = std::find_if(active.begin(), active.end(),
        [&](const auto& info) { return info.requestId == id; });
    REQUIRE(it != active.end());
    CHECK(it->assetType == resource::AssetType::WorldSector);
    CHECK(it->debugName == "sector(3,-2)");
    CHECK(it->stage == LoadStage::Pending);
    CHECK(it->computedPriority > 1.5f);

    // Restore slots and flush so the completion ring carries the type through too
    scheduler.init({});
    scheduler.update(glm::vec3(0.0f)); // dispatches (runs synchronously)
    scheduler.update(glm::vec3(0.0f)); // polls the completion

    auto record = findRecord(id);
    CHECK(record.finalStage == LoadStage::Completed);
    CHECK(record.assetType == resource::AssetType::WorldSector);
    CHECK(record.debugName == "sector(3,-2)");
}

TEST_CASE("promoting a queued prefetch lifts it out of the prefetch band")
{
    // A prefetched sector the camera reaches while its read is still queued must not stay behind
    // speculative asset work. WorldSectorServiceImpl::handleSectorLoad re-tags it on promotion.
    auto& scheduler = resource::ResourceLoadScheduler::instance();

    resource::ResourceSchedulerConfig hold;
    hold.maxConcurrentLoads = 0;
    scheduler.init(hold);
    // Pin the scheduler's camera to the origin: computePriority is distance-relative and the
    // singleton remembers whatever position the previous case passed.
    scheduler.update(glm::vec3(0.0f));

    auto guid = asset::AssetGUID::fromValue(0xFFF5'0000'0000'4321ull);

    resource::LoadRequest request;
    request.guid = guid;
    request.hint = prefetchHint(1024.0f);
    request.assetType = resource::AssetType::WorldSector;
    request.debugName = "sector prefetch(4,1)";
    request.executeLoad = []() {};
    uint64_t id = scheduler.submit(request);

    auto priorityOf = [&](uint64_t requestId)
    {
        auto active = scheduler.getActiveLoads();
        auto it = std::find_if(active.begin(), active.end(),
            [&](const auto& info) { return info.requestId == requestId; });
        REQUIRE(it != active.end());
        return it->computedPriority;
    };

    CHECK(priorityOf(id) < 1.5f);

    CHECK(scheduler.reprioritize(guid, activateHint(1024.0f)));
    CHECK(priorityOf(id) > 1.5f);

    // Once dispatched there is nothing left to reorder
    scheduler.init({});
    scheduler.update(glm::vec3(0.0f));
    scheduler.update(glm::vec3(0.0f));
    CHECK_FALSE(scheduler.reprioritize(guid, activateHint(0.0f)));
}

TEST_CASE("a sector cancelled before dispatch never reads the file")
{
    auto& scheduler = resource::ResourceLoadScheduler::instance();

    // Zero slots hold the request pending, exactly as a full scheduler would
    resource::ResourceSchedulerConfig config;
    config.maxConcurrentLoads = 0;
    scheduler.init(config);

    auto cancellation = resource::CancellationToken::create();
    bool ran = false;

    resource::LoadRequest request;
    request.guid = asset::AssetGUID::generate();
    request.hint = prefetchHint(2048.0f);
    request.assetType = resource::AssetType::WorldSector;
    request.debugName = "sector prefetch(8,8)";
    request.cancellation = cancellation;
    request.executeLoad = [&ran]() { ran = true; };
    uint64_t id = scheduler.submit(request);

    // The sector left the ring while its read was still queued
    cancellation->cancel();

    scheduler.init({});
    scheduler.update(glm::vec3(0.0f));

    CHECK_FALSE(ran);
    auto record = findRecord(id);
    CHECK(record.finalStage == LoadStage::Cancelled);
    CHECK(record.assetType == resource::AssetType::WorldSector);
}

}
