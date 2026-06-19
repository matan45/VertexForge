#include <doctest.h>
#include <resource/ResourceLoadScheduler.hpp>
#include <resource/ResourceLoadTypes.hpp>
#include <loading/LoadingProgress.hpp>
#include <asset/AssetGUID.hpp>
#include <algorithm>
#include <atomic>
#include <string>

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

// ============================================================
// Aggregate loading-progress phase blend (VK-1268). Pure helper:
// per-subsystem fractions -> single weighted 0-1 + current phase.
// ============================================================
TEST_SUITE("LoadingProgressAggregate")
{
    using loading::LoadingPhase;
    using loading::LoadingPhaseInputs;

    TEST_CASE("idle reports complete with no active phase")
    {
        auto p = loading::computeLoadingProgress({}, /*active=*/false);
        CHECK(p.fraction == doctest::Approx(1.0f));
        CHECK(p.phase == LoadingPhase::Idle);
    }

    TEST_CASE("all phases complete while active reports Complete")
    {
        auto p = loading::computeLoadingProgress({}, /*active=*/true);
        CHECK(p.fraction == doctest::Approx(1.0f));
        CHECK(p.phase == LoadingPhase::Complete);
    }

    TEST_CASE("nothing loaded yet sits at zero in the terrain phase")
    {
        LoadingPhaseInputs in{0.0f, 0.0f, 0.0f, 0.0f};
        auto p = loading::computeLoadingProgress(in);
        CHECK(p.fraction == doctest::Approx(0.0f));
        CHECK(p.phase == LoadingPhase::Terrain);
    }

    TEST_CASE("band weights follow the 40/30/20/10 split")
    {
        // Half terrain, rest done: 0.4*0.5 + 0.3 + 0.2 + 0.1 = 0.80
        auto t = loading::computeLoadingProgress({0.5f, 1.0f, 1.0f, 1.0f});
        CHECK(t.fraction == doctest::Approx(0.80f));
        CHECK(t.phase == LoadingPhase::Terrain);

        // Terrain done, sectors half: 0.4 + 0.3*0.5 + 0.2 + 0.1 = 0.85
        auto s = loading::computeLoadingProgress({1.0f, 0.5f, 1.0f, 1.0f});
        CHECK(s.fraction == doctest::Approx(0.85f));
        CHECK(s.phase == LoadingPhase::Sectors);

        // Terrain+sectors done, gpu half: 0.4 + 0.3 + 0.2*0.5 + 0.1 = 0.90
        auto g = loading::computeLoadingProgress({1.0f, 1.0f, 0.5f, 1.0f});
        CHECK(g.fraction == doctest::Approx(0.90f));
        CHECK(g.phase == LoadingPhase::GpuStreaming);

        // Only init left, half done: 0.4 + 0.3 + 0.2 + 0.1*0.5 = 0.95
        auto i = loading::computeLoadingProgress({1.0f, 1.0f, 1.0f, 0.5f});
        CHECK(i.fraction == doctest::Approx(0.95f));
        CHECK(i.phase == LoadingPhase::Initializing);
    }

    TEST_CASE("current phase is the earliest unfinished band")
    {
        // Later bands already at full should not pull the phase forward while
        // an earlier band is still incomplete.
        auto p = loading::computeLoadingProgress({0.2f, 1.0f, 1.0f, 1.0f});
        CHECK(p.phase == LoadingPhase::Terrain);
    }

    TEST_CASE("out-of-range fractions are clamped")
    {
        auto hi = loading::computeLoadingProgress({2.0f, 2.0f, 2.0f, 2.0f});
        CHECK(hi.fraction == doctest::Approx(1.0f));
        CHECK(hi.phase == LoadingPhase::Complete);

        auto lo = loading::computeLoadingProgress({-1.0f, -1.0f, -1.0f, -1.0f});
        CHECK(lo.fraction == doctest::Approx(0.0f));
        CHECK(lo.phase == LoadingPhase::Terrain);
    }

    TEST_CASE("phase labels match the ticket's status strings")
    {
        CHECK(std::string(loading::loadingPhaseLabel(LoadingPhase::Terrain)) ==
              "Generating terrain...");
        CHECK(std::string(loading::loadingPhaseLabel(LoadingPhase::Sectors)) ==
              "Loading sectors...");
        CHECK(std::string(loading::loadingPhaseLabel(LoadingPhase::GpuStreaming)) ==
              "Streaming resources...");
        CHECK(std::string(loading::loadingPhaseLabel(LoadingPhase::Initializing)) ==
              "Initializing...");
    }
}
