#include <doctest.h>
#include <cpumem/CpuMemoryManager.hpp>
#include <cpumem/ScopedCpuMemory.hpp>
#include <cpumem/CpuMemoryCategories.hpp>
#include <resource/ResourceLoadEstimate.hpp>
#include <resource/ResourceLoadScheduler.hpp>
#include <resource/ResourceLoadTypes.hpp>
#include <resource/AssetLifecycleManager.hpp>
#include <resource/AssetTypes.hpp>
#include <asset/AssetGUID.hpp>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// ============================================================
// VK-1434: CPU memory ownership service unit tests.
//
// CpuMemoryManager is a process-wide singleton, so state persists across
// cases. Tests therefore assert on DELTAS (capture-before / compare-after) and
// use unique category names, and always release every reservation / zero every
// category they touch so they don't pollute sibling cases.
// ============================================================

namespace
{
    constexpr uint64_t MB = 1024ull * 1024ull;

    std::optional<memory::CategoryView> findCategory(const memory::CpuMemorySnapshotData& snap,
                                                      const std::string& name)
    {
        for (const auto& c : snap.categories)
            if (c.name == name)
                return c;
        return std::nullopt;
    }
}

TEST_SUITE("CpuMemory")
{
    TEST_CASE("category registration is idempotent and stable (AC1, AC2)")
    {
        auto& m = memory::CpuMemoryManager::instance();

        memory::CategoryId a = m.registerCategory("test/dup", memory::CategoryKind::Transient);
        memory::CategoryId b = m.registerCategory("test/dup", memory::CategoryKind::Transient);
        CHECK(a != memory::kInvalidCategory);
        CHECK(a == b); // same name -> same id

        // Re-register with a differing kind: keeps the first kind, same id.
        memory::CategoryId c = m.registerCategory("test/dup", memory::CategoryKind::Staging);
        CHECK(c == a);

        auto snap = m.snapshot();
        auto view = findCategory(snap, "test/dup");
        REQUIRE(view.has_value());
        CHECK(view->kind == memory::CategoryKind::Transient); // original kind preserved
    }

    TEST_CASE("unknown / missing names are safe (AC9)")
    {
        auto& m = memory::CpuMemoryManager::instance();

        // Empty name -> invalid sentinel.
        CHECK(m.registerCategory("") == memory::kInvalidCategory);

        // Read of an invalid / out-of-range id returns 0, never throws, never creates.
        CHECK(m.usage(memory::kInvalidCategory) == 0);
        CHECK(m.usage(999999u) == 0);

        // Writes to an invalid / out-of-range id are no-ops.
        uint64_t before = m.totalTrackedBytes();
        m.addUsage(memory::kInvalidCategory, 100);
        m.addUsage(999999u, 100);
        m.setUsage(memory::kInvalidCategory, 100);
        m.subUsage(999999u, 100);
        CHECK(m.totalTrackedBytes() == before);
    }

    TEST_CASE("usage accounting round-trips and clamps at zero (AC3, AC4)")
    {
        auto& m = memory::CpuMemoryManager::instance();
        memory::CategoryId cat = m.registerCategory("test/usage", memory::CategoryKind::Transient);

        uint64_t catBefore = m.usage(cat);
        uint64_t totalBefore = m.totalTrackedBytes();

        m.addUsage(cat, 100);
        m.addUsage(cat, 50);
        CHECK(m.usage(cat) == catBefore + 150);
        CHECK(m.totalTrackedBytes() == totalBefore + 150);

        // Underflow clamps: subtracting more than present lands at 0, total returns
        // to the original baseline (never negative).
        m.subUsage(cat, 1000);
        CHECK(m.usage(cat) == 0);
        CHECK(m.totalTrackedBytes() == totalBefore - catBefore);

        // setUsage is absolute.
        m.setUsage(cat, 777);
        CHECK(m.usage(cat) == 777);
        m.setUsage(cat, 0); // cleanup
        CHECK(m.usage(cat) == 0);
    }

    TEST_CASE("peak watermark tracks the high-water mark (AC8)")
    {
        auto& m = memory::CpuMemoryManager::instance();
        memory::CategoryId cat = m.registerCategory("test/peak", memory::CategoryKind::Transient);

        m.addUsage(cat, 200);
        m.subUsage(cat, 150); // current 50, peak should hold 200

        auto snap = m.snapshot();
        auto view = findCategory(snap, "test/peak");
        REQUIRE(view.has_value());
        CHECK(view->bytes == 50);
        CHECK(view->peak >= 200);

        m.subUsage(cat, 50); // cleanup
    }

    TEST_CASE("ScopedCpuMemory adds on construct, subtracts on destruct, move-safe (AC3, AC7)")
    {
        auto& m = memory::CpuMemoryManager::instance();
        memory::CategoryId cat = m.registerCategory("test/scoped", memory::CategoryKind::Transient);
        uint64_t before = m.usage(cat);

        {
            memory::ScopedCpuMemory guard(cat, 1234);
            CHECK(m.usage(cat) == before + 1234);

            // Moving must not double-count: g2 owns the charge, guard is neutered.
            memory::ScopedCpuMemory g2(std::move(guard));
            CHECK(m.usage(cat) == before + 1234);
        }
        // g2 destroyed (subtract once), guard destroyed (no-op) -> back to baseline.
        CHECK(m.usage(cat) == before);
    }

    TEST_CASE("pre-load gate admits under budget and defers over budget (AC5, AC10)")
    {
        auto& m = memory::CpuMemoryManager::instance();
        uint64_t base = m.outstandingReservations();

        m.setBudget(1000);

        CHECK(m.reserve(9001, 400, /*external*/ 0));
        CHECK(m.outstandingReservations() == base + 400);

        CHECK(m.reserve(9002, 400, 0)); // 800 <= 1000
        CHECK(m.outstandingReservations() == base + 800);

        // 800 + 400 > 1000 -> deferred, NOT recorded.
        CHECK_FALSE(m.reserve(9003, 400, 0));
        CHECK(m.outstandingReservations() == base + 800);

        // External committed bytes count toward the denominator.
        CHECK_FALSE(m.reserve(9004, 100, /*external*/ 950)); // 950 + 800 + 100 > 1000

        // Critical loads bypass the gate entirely (deadlock safety).
        m.reserveUnconditional(9005, 5000);
        CHECK(m.outstandingReservations() == base + 800 + 5000);

        m.releaseReservation(9001);
        m.releaseReservation(9002);
        m.releaseReservation(9005);
        m.releaseReservation(424242); // unknown id -> no-op
        CHECK(m.outstandingReservations() == base);

        m.setBudget(0); // cleanup / disabled
    }

    TEST_CASE("budget 0 is advisory: the gate always admits")
    {
        auto& m = memory::CpuMemoryManager::instance();
        uint64_t base = m.outstandingReservations();

        m.setBudget(0);
        CHECK(m.reserve(9100, (1ull << 60), (1ull << 60))); // absurd sizes still admitted
        CHECK(m.outstandingReservations() == base + (1ull << 60));

        m.releaseReservation(9100);
        CHECK(m.outstandingReservations() == base);
    }

    TEST_CASE("staging usage is tracked but excluded from the gate denominator (AC6)")
    {
        auto& m = memory::CpuMemoryManager::instance();
        memory::CategoryId stag = m.registerCategory(memory::categories::UploadStagingRing,
                                                     memory::CategoryKind::Staging);
        uint64_t totalBefore = m.totalTrackedBytes();
        uint64_t resvBefore = m.outstandingReservations();

        m.setUsage(stag, 500 * MB);
        CHECK(m.totalTrackedBytes() == totalBefore + 500 * MB);

        // Budget far below the staging total. The gate denominator is
        // (externalDecoded + reservations), NOT totalTrackedBytes(), so a small
        // load still admits despite 500 MB of staging in flight.
        m.setBudget(100 * MB);
        CHECK(m.reserve(9200, 1 * MB, /*external*/ 0));
        CHECK(m.outstandingReservations() == resvBefore + 1 * MB);

        m.releaseReservation(9200);
        m.setUsage(stag, 0);
        m.setBudget(0);
        CHECK(m.totalTrackedBytes() == totalBefore);
        CHECK(m.outstandingReservations() == resvBefore);
    }

    TEST_CASE("snapshot exposes categories, totals, budget and gate state (AC8)")
    {
        auto& m = memory::CpuMemoryManager::instance();
        memory::CategoryId cat = m.registerCategory("test/snapshot", memory::CategoryKind::Transient);
        m.addUsage(cat, 42);
        m.setGateState(memory::GateState::Closed, 7);
        m.setBudget(2048);

        auto snap = m.snapshot();
        auto view = findCategory(snap, "test/snapshot");
        REQUIRE(view.has_value());
        CHECK(view->bytes == 42);
        CHECK(snap.budgetBytes == 2048);
        CHECK(snap.gateState == memory::GateState::Closed);
        CHECK(snap.deferredLoadCount == 7);
        CHECK(snap.totalTrackedBytes >= 42);

        // cleanup
        m.subUsage(cat, 42);
        m.setGateState(memory::GateState::Open, 0);
        m.setBudget(0);
    }
}

// ============================================================
// VK-1434: pre-decode RAM estimator feeding the gate.
//
// estimatePreDecodeBytes errs HIGH on purpose (never under-estimates) so the
// gate cannot admit a load that would then blow the budget, and returns 0 for
// an undeterminable size ("ungated by design"). These tests are fully
// self-contained: they write tiny temp files in the scratch dir and delete
// them, so they pull in no real assets.
// ============================================================
namespace
{
    // Per-session scratch dir (mirrors the harness scratchpad); unique-ish file
    // names per case so parallel-unlikely-but-possible reruns don't collide.
    std::filesystem::path scratchFile(const char* leaf)
    {
        auto dir = std::filesystem::temp_directory_path() / "vf_cpumem_estimate_tests";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        return dir / leaf;
    }

    // Write `size` arbitrary bytes and return the absolute path as a string.
    std::string writeSizedFile(const char* leaf, uint64_t size)
    {
        auto path = scratchFile(leaf);
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        std::vector<char> bytes(static_cast<size_t>(size), '\1');
        f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        f.close();
        return path.string();
    }

    void putLE32(std::vector<unsigned char>& buf, size_t at, uint32_t v)
    {
        if (buf.size() < at + 4)
            buf.resize(at + 4, 0);
        buf[at + 0] = static_cast<unsigned char>(v & 0xFF);
        buf[at + 1] = static_cast<unsigned char>((v >> 8) & 0xFF);
        buf[at + 2] = static_cast<unsigned char>((v >> 16) & 0xFF);
        buf[at + 3] = static_cast<unsigned char>((v >> 24) & 0xFF);
    }
}

TEST_SUITE("ResourceLoadEstimate")
{
    using resource::AssetType;
    using resource::estimatePreDecodeBytes;

    TEST_CASE("missing / empty path is ungated (returns 0)")
    {
        // Path that cannot exist -> file_size fails -> 0 ("ungated by design").
        CHECK(estimatePreDecodeBytes(AssetType::Texture, "") == 0);
        CHECK(estimatePreDecodeBytes(AssetType::Mesh,
              (scratchFile("does_not_exist.vfMesh")).string()) == 0);
        // Audio takes the header path, but a missing file still yields 0
        // (the fileSizeOf guard returns before the header read).
        CHECK(estimatePreDecodeBytes(AssetType::Audio,
              (scratchFile("does_not_exist.vfAudio")).string()) == 0);
    }

    TEST_CASE("texture / HDR estimate >= file size (small inline-mip headroom)")
    {
        const uint64_t fileBytes = 4096;
        std::string tex = writeSizedFile("estimate_tex.vfImage", fileBytes);
        std::string hdr = writeSizedFile("estimate_hdr.vfHdr", fileBytes);

        // Source: fileBytes + fileBytes/16 == ~1.0625x. Conservative (>= plausible
        // held bytes, which for inline-mip formats are ~= file size).
        const uint64_t expected = fileBytes + fileBytes / 16;
        CHECK(estimatePreDecodeBytes(AssetType::Texture, tex) == expected);
        CHECK(estimatePreDecodeBytes(AssetType::HDR, hdr) == expected);
        CHECK(estimatePreDecodeBytes(AssetType::Texture, tex) >= fileBytes);

        std::error_code ec;
        std::filesystem::remove(tex, ec);
        std::filesystem::remove(hdr, ec);
    }

    TEST_CASE("mesh / animation / default use conservative per-type multipliers")
    {
        const uint64_t fileBytes = 1000;
        std::string mesh = writeSizedFile("estimate_mesh.vfMesh", fileBytes);

        // Source multipliers: Mesh 3x, Animation 2x, default (e.g. Font) 2x.
        CHECK(estimatePreDecodeBytes(AssetType::Mesh, mesh) == fileBytes * 3);
        CHECK(estimatePreDecodeBytes(AssetType::Animation, mesh) == fileBytes * 2);
        CHECK(estimatePreDecodeBytes(AssetType::Font, mesh) == fileBytes * 2);

        // Every multiplier is >= 1x: the estimate never under-states the file.
        CHECK(estimatePreDecodeBytes(AssetType::Mesh, mesh) >= fileBytes);
        CHECK(estimatePreDecodeBytes(AssetType::Animation, mesh) >= fileBytes);
        CHECK(estimatePreDecodeBytes(AssetType::Font, mesh) >= fileBytes);

        std::error_code ec;
        std::filesystem::remove(mesh, ec);
    }

    TEST_CASE("audio reads the header and computes exact PCM size (frames*channels*2)")
    {
        // .vfAudio layout per ResourceLoadEstimate.hpp: channels (LE u32) @ byte 19,
        // per-channel frame count (LE u32) @ byte 23. Decoded PCM = frames*channels*2.
        const uint32_t channels = 2;
        const uint32_t frames = 48000; // 1 second @ 48 kHz, plausible & non-trivial

        std::vector<unsigned char> buf(27, 0); // header up to byte 23 + the 4-byte frame field
        putLE32(buf, 19, channels);
        putLE32(buf, 23, frames);

        auto path = scratchFile("estimate_audio.vfAudio");
        {
            std::ofstream f(path, std::ios::binary | std::ios::trunc);
            f.write(reinterpret_cast<const char*>(buf.data()),
                    static_cast<std::streamsize>(buf.size()));
        }

        const uint64_t expectedPcm =
            static_cast<uint64_t>(frames) * channels * sizeof(short);
        CHECK(estimatePreDecodeBytes(AssetType::Audio, path.string()) == expectedPcm);

        // PCM is ~vastly larger than the tiny header file: the header math is what
        // saves us from the Vorbis-compression under-estimate.
        CHECK(expectedPcm > buf.size());

        std::error_code ec;
        std::filesystem::remove(path, ec);
    }

    TEST_CASE("audio with an implausible header falls back to a 12x expansion")
    {
        // channels==0 is implausible -> estimateAudioPcmBytes returns 0 -> caller
        // falls back to fileBytes * 12 (a conservative Vorbis->PCM factor).
        const uint64_t fileBytes = 64;
        std::vector<unsigned char> buf(27, 0); // channels @19 left 0 -> implausible
        putLE32(buf, 23, 1000);
        buf.resize(static_cast<size_t>(fileBytes), 0);

        auto path = scratchFile("estimate_audio_bad.vfAudio");
        {
            std::ofstream f(path, std::ios::binary | std::ios::trunc);
            f.write(reinterpret_cast<const char*>(buf.data()),
                    static_cast<std::streamsize>(buf.size()));
        }

        CHECK(estimatePreDecodeBytes(AssetType::Audio, path.string()) == fileBytes * 12);

        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
}

// ============================================================
// VK-1434: end-to-end gate behavior over the REAL ResourceLoadScheduler.
//
// In the test runner the JobSystem is uninitialized, so JobSystem::submit runs
// the load lambda INLINE (JobSystem.cpp submitTask: "runs inline if the system
// is not initialized"). Consequences exploited below:
//   * A dispatched load's future is ready immediately; the load is finalized and
//     its reservation released on the NEXT dispatchPending/pollCompletions pass
//     (dispatchPending() polls completions at the top), i.e. the next update().
//   * A request the gate DEFERS never runs and stays in pendingRequests, so we
//     can observe Pending across update() ticks until headroom frees up.
//
// The scheduler is a process-wide singleton; cases find their own records by
// requestId and reset the budget to 0 (gate Open) on the way out so they don't
// perturb the CpuMemory suite. The gate's external denominator is
// AssetLifecycleManager::getTotalTrackedBytes(); we capture it and size the
// budget relative to it so the decision is deterministic regardless of any
// bytes a sibling suite happens to have tracked.
// ============================================================
TEST_SUITE("CpuMemoryGateScheduler")
{
    using resource::LoadRequest;
    using resource::LoadStage;
    using resource::LoadImportance;
    using resource::ResourceLoadScheduler;
    using resource::ResourceSchedulerConfig;

    namespace
    {
        std::optional<resource::CompletedLoadRecord> findCompletion(uint64_t requestId)
        {
            auto records = ResourceLoadScheduler::instance().getRecentCompletions();
            for (const auto& r : records)
                if (r.requestId == requestId)
                    return r;
            return std::nullopt;
        }

        bool isPending(const asset::AssetGUID& guid)
        {
            auto p = ResourceLoadScheduler::instance().findProgress(guid);
            return p != nullptr && p->stage() == LoadStage::Pending;
        }
    }

    TEST_CASE("non-Critical load is deferred over budget then dispatches after headroom frees")
    {
        auto& scheduler = ResourceLoadScheduler::instance();
        auto& cpuMem = memory::CpuMemoryManager::instance();
        auto& lifecycle = resource::AssetLifecycleManager::instance();

        // Generous slot count so deferral is purely the memory gate, not the
        // concurrency cap.
        ResourceSchedulerConfig cfg;
        cfg.maxConcurrentLoads = 8;
        scheduler.init(cfg);

        // Occupy most of the budget headroom with a manually-held reservation so
        // the scheduler's gate must DEFER the load. (We can't keep an *in-flight*
        // load's reservation outstanding to do this: the JobSystem runs loads
        // inline, so an in-flight load's reservation is reaped by pollCompletions
        // at the top of the very next dispatch. A standalone reservation we own is
        // the deterministic way to hold headroom.)
        //
        // budget = decodedTotal + resvBase + 1500. With a held 1000 + the load's
        // own 1000, the gate sees decodedTotal + (resvBase+1000) + 1000 > budget
        // -> defer. After releasing the held 1000, decodedTotal + resvBase + 1000
        // <= budget -> admit on the next pass. Sizing relative to resvBase makes
        // the admit/defer decision deterministic regardless of any reservations a
        // sibling case happens to hold.
        const uint64_t decodedTotal = lifecycle.getTotalTrackedBytes();
        const uint64_t est = 1000;
        const uint64_t resvBase = cpuMem.outstandingReservations();
        cpuMem.setBudget(decodedTotal + resvBase + 1500);

        constexpr uint64_t kHoldId = 0xCAFEBABEull; // distinct from scheduler requestIds
        cpuMem.reserveUnconditional(kHoldId, est); // occupy headroom
        CHECK(cpuMem.outstandingReservations() == resvBase + est);

        bool ranB = false;
        LoadRequest b;
        b.guid = asset::AssetGUID::generate();
        b.assetType = resource::AssetType::Mesh;
        b.estimatedBytes = est;
        b.executeLoad = [&ranB] { ranB = true; };

        // submit() dispatches eagerly. B is over budget (held + B's est) -> the
        // gate defers it: it never runs and stays pending (never dropped).
        uint64_t idB = scheduler.submit(b);
        CHECK_FALSE(ranB);
        CHECK(isPending(b.guid));
        // The gate did NOT take a reservation for the deferred load.
        CHECK(cpuMem.outstandingReservations() == resvBase + est);

        // An update() tick while still over budget keeps B deferred (retried, not
        // dropped) — proves the deferral survives across ticks.
        scheduler.update({0.0f, 0.0f, 0.0f});
        CHECK_FALSE(ranB);
        CHECK(isPending(b.guid));

        // Free the held headroom; the next dispatch admits B (1000 <= 1500).
        cpuMem.releaseReservation(kHoldId);
        scheduler.update({0.0f, 0.0f, 0.0f});
        CHECK(ranB);

        // One more tick polls B's ready future and releases its reservation.
        scheduler.update({0.0f, 0.0f, 0.0f});
        auto recB = findCompletion(idB);
        REQUIRE(recB.has_value());
        CHECK(recB->finalStage == LoadStage::Completed);

        // No reservation leaked: back to the baseline we captured.
        CHECK(cpuMem.outstandingReservations() == resvBase);

        cpuMem.setBudget(0);
        cpuMem.setGateState(memory::GateState::Open, 0);
    }

    TEST_CASE("Critical load bypasses the gate even when over budget")
    {
        auto& scheduler = ResourceLoadScheduler::instance();
        auto& cpuMem = memory::CpuMemoryManager::instance();
        auto& lifecycle = resource::AssetLifecycleManager::instance();

        scheduler.init({}); // default slots

        // Budget of decodedTotal + 1: any non-trivial estimate is over budget.
        const uint64_t decodedTotal = lifecycle.getTotalTrackedBytes();
        cpuMem.setBudget(decodedTotal + 1);
        const uint64_t resvBase = cpuMem.outstandingReservations();

        bool ranCritical = false;
        LoadRequest c;
        c.guid = asset::AssetGUID::generate();
        c.assetType = resource::AssetType::Texture;
        c.estimatedBytes = 10 * 1024 * 1024; // 10 MB, far over a 1-byte headroom
        c.hint.importance = LoadImportance::Critical;
        c.executeLoad = [&ranCritical] { ranCritical = true; };

        uint64_t idC = scheduler.submit(c);

        CHECK(ranCritical); // dispatched despite being over budget (deadlock safety)

        // Drain: poll the ready future so the unconditional reservation is released.
        scheduler.update({0.0f, 0.0f, 0.0f});
        auto recC = findCompletion(idC);
        REQUIRE(recC.has_value());
        CHECK(recC->finalStage == LoadStage::Completed);
        CHECK(cpuMem.outstandingReservations() == resvBase); // no leak

        cpuMem.setBudget(0);
        cpuMem.setGateState(memory::GateState::Open, 0);
    }

    TEST_CASE("a load cancelled before dispatch leaks no reservation")
    {
        auto& scheduler = ResourceLoadScheduler::instance();
        auto& cpuMem = memory::CpuMemoryManager::instance();

        // Zero slots keep the load Pending (concurrency cap), so we can cancel it
        // before it ever dispatches and reserves.
        ResourceSchedulerConfig cfg;
        cfg.maxConcurrentLoads = 0;
        scheduler.init(cfg);

        cpuMem.setBudget(0); // gate advisory: deferral here is purely the slot cap
        const uint64_t resvBase = cpuMem.outstandingReservations();

        auto cancellation = resource::CancellationToken::create();
        bool ran = false;
        LoadRequest r;
        r.guid = asset::AssetGUID::generate();
        r.assetType = resource::AssetType::Mesh;
        r.estimatedBytes = 4096;
        r.cancellation = cancellation;
        r.executeLoad = [&ran] { ran = true; };

        uint64_t id = scheduler.submit(r); // stays pending (0 slots)
        CHECK(isPending(r.guid));
        CHECK(cpuMem.outstandingReservations() == resvBase); // never reserved while pending

        // Cancel before any dispatch, then pump: the request is dropped from
        // pending and recorded Cancelled; it never ran and never reserved.
        cancellation->cancel();
        scheduler.init({}); // restore slots
        scheduler.update({0.0f, 0.0f, 0.0f});

        CHECK_FALSE(ran);
        auto rec = findCompletion(id);
        REQUIRE(rec.has_value());
        CHECK(rec->finalStage == LoadStage::Cancelled);
        CHECK(cpuMem.outstandingReservations() == resvBase); // no leak

        cpuMem.setGateState(memory::GateState::Open, 0);
    }

    TEST_CASE("a failed load still releases its reservation")
    {
        auto& scheduler = ResourceLoadScheduler::instance();
        auto& cpuMem = memory::CpuMemoryManager::instance();

        scheduler.init({});
        cpuMem.setBudget(0); // advisory: admit so we can exercise the failed-release path
        const uint64_t resvBase = cpuMem.outstandingReservations();

        auto progress = resource::LoadProgress::create();
        LoadRequest r;
        r.guid = asset::AssetGUID::generate();
        r.assetType = resource::AssetType::Mesh;
        r.estimatedBytes = 8192;
        r.progress = progress;
        r.executeLoad = [progress] { progress->setStage(LoadStage::Failed); };

        uint64_t id = scheduler.submit(r); // dispatches + runs inline -> marks Failed

        // The reservation is live until the completion is polled.
        scheduler.update({0.0f, 0.0f, 0.0f}); // polls the ready future, releases reservation

        auto rec = findCompletion(id);
        REQUIRE(rec.has_value());
        CHECK(rec->finalStage == LoadStage::Failed);
        CHECK(cpuMem.outstandingReservations() == resvBase); // released even on failure

        cpuMem.setBudget(0);
        cpuMem.setGateState(memory::GateState::Open, 0);
    }
}
