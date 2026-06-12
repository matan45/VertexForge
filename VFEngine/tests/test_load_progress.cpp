#include <doctest.h>
#include <resource/ResourceLoadScheduler.hpp>
#include <resource/ResourceLoadTypes.hpp>
#include <asset/AssetGUID.hpp>
#include <algorithm>
#include <atomic>

// ============================================================
// ResourceLoadScheduler progress + timing capture. JobSystem is
// uninitialized in tests, so dispatched loads execute
// synchronously inside submit()/update().
//
// The scheduler is a process-wide singleton — cases search the
// completion ring by requestId instead of assuming it is empty.
// ============================================================

namespace
{
    using resource::LoadStage;

    resource::CompletedLoadRecord findRecord(uint64_t requestId)
    {
        auto records = resource::ResourceLoadScheduler::instance().getRecentCompletions();
        auto it = std::find_if(records.begin(), records.end(),
            [&](const auto& r) { return r.requestId == requestId; });
        REQUIRE(it != records.end());
        return *it;
    }
}

TEST_SUITE("ResourceLoadScheduler")
{

TEST_CASE("successful load transitions Pending -> Loading -> Completed")
{
    auto& scheduler = resource::ResourceLoadScheduler::instance();
    scheduler.init({});

    auto progress = resource::LoadProgress::create();
    std::atomic<int> observedStage{-1};

    resource::LoadRequest request;
    request.guid = asset::AssetGUID::generate();
    request.progress = progress;
    request.executeLoad = [progress, &observedStage]()
    {
        // Synchronous JobSystem: we run during dispatch, after the
        // scheduler marked us Loading
        observedStage.store(static_cast<int>(progress->stage()));
        progress->setBytes(4096);
    };

    uint64_t id = scheduler.submit(request);
    scheduler.update({0.0f, 0.0f, 0.0f});

    CHECK(observedStage.load() == static_cast<int>(LoadStage::Loading));
    CHECK(progress->stage() == LoadStage::Completed);
    CHECK(progress->fraction() == 1.0f);
    CHECK(progress->isTerminal());

    auto record = findRecord(id);
    CHECK(record.finalStage == LoadStage::Completed);
    CHECK(record.bytes == 4096);
    CHECK(record.queueWaitMs >= 0.0f);
    CHECK(record.loadMs >= 0.0f);
}

TEST_CASE("loader-reported failure survives as Failed")
{
    auto& scheduler = resource::ResourceLoadScheduler::instance();
    scheduler.init({});

    auto progress = resource::LoadProgress::create();

    resource::LoadRequest request;
    request.guid = asset::AssetGUID::generate();
    request.progress = progress;
    request.executeLoad = [progress]()
    {
        // Loader lambdas swallow exceptions into null results and report
        // through the progress handle
        progress->setStage(LoadStage::Failed);
    };

    uint64_t id = scheduler.submit(request);
    scheduler.update({0.0f, 0.0f, 0.0f});

    CHECK(progress->stage() == LoadStage::Failed);
    CHECK(findRecord(id).finalStage == LoadStage::Failed);
}

TEST_CASE("pre-cancelled request never runs and records Cancelled")
{
    auto& scheduler = resource::ResourceLoadScheduler::instance();
    scheduler.init({});

    auto progress = resource::LoadProgress::create();
    auto cancellation = resource::CancellationToken::create();
    cancellation->cancel();

    bool ran = false;
    resource::LoadRequest request;
    request.guid = asset::AssetGUID::generate();
    request.progress = progress;
    request.cancellation = cancellation;
    request.executeLoad = [&ran]() { ran = true; };

    uint64_t id = scheduler.submit(request);
    scheduler.update({0.0f, 0.0f, 0.0f});

    CHECK_FALSE(ran);
    CHECK(progress->stage() == LoadStage::Cancelled);
    CHECK(findRecord(id).finalStage == LoadStage::Cancelled);
}

TEST_CASE("Critical importance bypasses the concurrency cap")
{
    auto& scheduler = resource::ResourceLoadScheduler::instance();

    // Zero slots: normal loads can never dispatch, critical ones must
    resource::ResourceSchedulerConfig config;
    config.maxConcurrentLoads = 0;
    scheduler.init(config);

    bool normalRan = false;
    resource::LoadRequest normalRequest;
    normalRequest.guid = asset::AssetGUID::generate();
    normalRequest.executeLoad = [&normalRan]() { normalRan = true; };
    scheduler.submit(normalRequest);

    bool criticalRan = false;
    resource::LoadRequest criticalRequest;
    criticalRequest.guid = asset::AssetGUID::generate();
    criticalRequest.hint.importance = resource::LoadImportance::Critical;
    criticalRequest.executeLoad = [&criticalRan]() { criticalRan = true; };
    scheduler.submit(criticalRequest);

    CHECK_FALSE(normalRan);
    CHECK(criticalRan);

    // Drain: restore real slots so the normal load completes too
    scheduler.init({});
    scheduler.update({0.0f, 0.0f, 0.0f});
    CHECK(normalRan);
}

TEST_CASE("findProgress sees pending loads and goes null after completion")
{
    auto& scheduler = resource::ResourceLoadScheduler::instance();

    // Zero slots keep the load pending so findProgress can observe it
    resource::ResourceSchedulerConfig config;
    config.maxConcurrentLoads = 0;
    scheduler.init(config);

    auto guid = asset::AssetGUID::generate();
    resource::LoadRequest request;
    request.guid = guid;
    request.executeLoad = []() {};
    scheduler.submit(request);

    auto pendingProgress = scheduler.findProgress(guid);
    REQUIRE(pendingProgress != nullptr);
    CHECK(pendingProgress->stage() == LoadStage::Pending);

    auto active = scheduler.getActiveLoads();
    auto it = std::find_if(active.begin(), active.end(),
        [&](const auto& info) { return info.guid == guid; });
    REQUIRE(it != active.end());
    CHECK(it->stage == LoadStage::Pending);
    CHECK(it->queueWaitMs >= 0.0f);

    scheduler.init({});
    scheduler.update({0.0f, 0.0f, 0.0f}); // dispatches (runs synchronously)
    scheduler.update({0.0f, 0.0f, 0.0f}); // polls the completion

    CHECK(scheduler.findProgress(guid) == nullptr);
    CHECK(pendingProgress->stage() == LoadStage::Completed);
}

}
