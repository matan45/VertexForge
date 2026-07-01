#include <doctest.h>

// VK-1453 (VFXSequence Phase 4) — the debug/diagnostics collection surfaces:
//   1. VFXRuntimeDiagnostics: a process-wide, deduplicating, bounded warning ring.
//   2. GetVFXComboStatsQuery: combo counts dispatched through the sequence service.

#include <impl/vfx/VFXSequenceRuntimeServiceImpl.hpp>
#include <events/EventDispatcher.hpp>
#include <events/vfx/VFXRuntimeEvents.hpp>
#include <events/vfx/VFXSequenceRuntimeEvents.hpp>
#include <asset/AssetDatabase.hpp>
#include <asset/AssetGUID.hpp>
#include <asset/AssetRef.hpp>
#include <vfx/VFXAsset.hpp>
#include <vfx/VFXRuntimeDiagnostics.hpp>
#include <vfx/VFXSequenceAsset.hpp>
#include <vfx/VFXSequenceTypes.hpp>
#include <vfx/VFXTypes.hpp>

#include <glm/glm.hpp>
#include <algorithm>
#include <filesystem>
#include <set>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace
{
    namespace fs = std::filesystem;
    namespace vfxruntime = services::events::vfxruntime;
    namespace vfxsequence = services::events::vfxsequence;

    template <typename Fn>
    class ScopeExit
    {
    public:
        explicit ScopeExit(Fn fn) : fn(std::move(fn)) {}
        ~ScopeExit() { fn(); }
        ScopeExit(const ScopeExit&) = delete;
        ScopeExit& operator=(const ScopeExit&) = delete;

    private:
        Fn fn;
    };

    template <typename Fn>
    ScopeExit<Fn> makeScopeExit(Fn fn)
    {
        return ScopeExit<Fn>(std::move(fn));
    }

    fs::path testRoot()
    {
        return fs::temp_directory_path() / "vf_vfx_debug_tests";
    }

    std::string saveChildVFX(const std::string& fileName)
    {
        const vfx::VFXData data = vfx::VFXAsset::createDefault("debug_child");
        std::error_code ec;
        fs::create_directories(testRoot(), ec);
        const fs::path path = testRoot() / fileName;
        REQUIRE(vfx::VFXAsset::save(path.string(), data));
        return path.string();
    }

    asset::AssetRef makeChildRef(uint64_t guidValue, const std::string& diskPath)
    {
        const auto guid = asset::AssetGUID::fromValue(guidValue);
        asset::AssetDatabase::instance().registerAssetWithGUID(guid, diskPath, resource::AssetType::VFX);
        return asset::AssetRef::fromGUID(guid);
    }

    std::string saveSequence(const std::string& fileName, const vfx::VFXSequenceData& data)
    {
        std::error_code ec;
        fs::create_directories(testRoot(), ec);
        const fs::path path = testRoot() / fileName;
        REQUIRE(vfx::VFXSequenceAsset::save(data, path.string()));
        return path.string();
    }

    struct MockVFXRuntime
    {
        services::VFXInstanceId nextId = 8000;
        std::set<services::VFXInstanceId> live;

        void install()
        {
            auto& d = ::events::EventDispatcher::instance();
            d.registerCommandHandler<vfxruntime::CreateVFXInstanceCommand>(
                [this](const vfxruntime::CreateVFXInstanceCommand&) -> services::VFXInstanceId
                {
                    const services::VFXInstanceId id = nextId++;
                    live.insert(id);
                    return id;
                });
            d.registerCommandHandler<vfxruntime::DestroyVFXInstanceCommand>(
                [this](const vfxruntime::DestroyVFXInstanceCommand& c) { live.erase(c.instanceId); });
            d.registerCommandHandler<vfxruntime::SetVFXInstanceTransformCommand>(
                [](const vfxruntime::SetVFXInstanceTransformCommand&) {});
            d.registerCommandHandler<vfxruntime::ApplyVFXInstanceOverridesCommand>(
                [](const vfxruntime::ApplyVFXInstanceOverridesCommand&) {});
            d.registerCommandHandler<vfxruntime::PlayVFXInstanceCommand>(
                [](const vfxruntime::PlayVFXInstanceCommand&) {});
            d.registerCommandHandler<vfxruntime::StopVFXInstanceCommand>(
                [](const vfxruntime::StopVFXInstanceCommand&) {});
            d.registerQueryHandler<vfxruntime::IsVFXInstancePlayingQuery>(
                [this](const vfxruntime::IsVFXInstancePlayingQuery& q) -> bool
                {
                    return live.count(q.instanceId) > 0;
                });
        }
    };

    void unregisterAll()
    {
        auto& d = ::events::EventDispatcher::instance();
        d.unregisterCommandHandler<vfxruntime::CreateVFXInstanceCommand>();
        d.unregisterCommandHandler<vfxruntime::DestroyVFXInstanceCommand>();
        d.unregisterCommandHandler<vfxruntime::SetVFXInstanceTransformCommand>();
        d.unregisterCommandHandler<vfxruntime::ApplyVFXInstanceOverridesCommand>();
        d.unregisterCommandHandler<vfxruntime::PlayVFXInstanceCommand>();
        d.unregisterCommandHandler<vfxruntime::StopVFXInstanceCommand>();
        d.unregisterQueryHandler<vfxruntime::IsVFXInstancePlayingQuery>();
        d.unregisterCommandHandler<vfxsequence::CreateVFXComboInstanceCommand>();
        d.unregisterCommandHandler<vfxsequence::DestroyVFXComboInstanceCommand>();
        d.unregisterCommandHandler<vfxsequence::PlayVFXComboInstanceCommand>();
        d.unregisterCommandHandler<vfxsequence::StopVFXComboInstanceCommand>();
        d.unregisterCommandHandler<vfxsequence::ResetVFXComboInstanceCommand>();
        d.unregisterCommandHandler<vfxsequence::SetVFXComboInstanceTransformCommand>();
        d.unregisterCommandHandler<vfxsequence::AttachVFXComboInstanceToSocketCommand>();
        d.unregisterCommandHandler<vfxsequence::DetachVFXComboInstanceCommand>();
        d.unregisterCommandHandler<vfxsequence::TriggerVFXComboCueCommand>();
        d.unregisterCommandHandler<vfxsequence::UpdateVFXSequenceRuntimeCommand>();
        d.unregisterCommandHandler<vfxsequence::SetVFXComboPausedCommand>();
        d.unregisterCommandHandler<vfxsequence::SetVFXComboPlaybackRateCommand>();
        d.unregisterCommandHandler<vfxsequence::SeekVFXComboCommand>();
        d.unregisterQueryHandler<vfxsequence::IsVFXComboInstancePlayingQuery>();
        d.unregisterQueryHandler<vfxsequence::GetVFXComboStatsQuery>();
    }
}

TEST_SUITE("VFXDebugCollection")
{
    TEST_CASE("VFXRuntimeDiagnostics dedups repeats and keeps a count")
    {
        auto& diag = vfx::VFXRuntimeDiagnostics::instance();
        diag.clear();

        CHECK(diag.report("VFXSequence", "boom") == true);   // first sighting => log-once
        CHECK(diag.report("VFXSequence", "boom") == false);  // repeat => bump count only
        CHECK(diag.report("VFXSequence", "boom") == false);

        CHECK(diag.distinctCount() == 1);
        const auto recent = diag.recent();
        REQUIRE(recent.size() == 1);
        CHECK(recent.front().source == "VFXSequence");
        CHECK(recent.front().message == "boom");
        CHECK(recent.front().count == 3);

        // A different message is a distinct entry.
        CHECK(diag.report("VFXSequence", "fizz") == true);
        CHECK(diag.distinctCount() == 2);
    }

    TEST_CASE("VFXRuntimeDiagnostics ring is bounded")
    {
        auto& diag = vfx::VFXRuntimeDiagnostics::instance();
        diag.clear();

        for (int i = 0; i < 100; ++i)
            diag.report("VFXSequence", "msg-" + std::to_string(i));

        // Capacity is 64 distinct entries; the oldest are evicted.
        CHECK(diag.distinctCount() <= 64);
        CHECK(diag.distinctCount() == 64);
        CHECK(diag.recent(64).size() == 64);

        diag.clear();
        CHECK(diag.distinctCount() == 0);
        CHECK(diag.recent().empty());
    }

    TEST_CASE("GetVFXComboStatsQuery reports active/playing/live combo counts")
    {
        asset::AssetDatabase::instance().clear();
        MockVFXRuntime mock;
        mock.install();

        const std::string childPath = saveChildVFX("stats_child.vfVFX");
        const auto childRef = makeChildRef(0xD001, childPath);
        vfx::VFXSequenceData seq;
        seq.name = "stats";
        vfx::VFXSequenceStep step;
        step.vfxRef = childRef;
        step.startTime = 0.0f;
        seq.steps.push_back(step);
        const std::string seqPath = saveSequence("Stats.vfVFXSequence", seq);

        services::VFXSequenceRuntimeServiceImpl svc;
        svc.registerEventHandlers();
        auto cleanup = makeScopeExit(unregisterAll);

        constexpr int kCombos = 3;
        std::vector<services::VFXComboInstanceId> combos;
        for (int i = 0; i < kCombos; ++i)
        {
            const auto id = svc.createCombo(seqPath, glm::mat4(1.0f), 0, false);
            REQUIRE(id != 0);
            combos.push_back(id);
        }

        auto& dispatcher = ::events::EventDispatcher::instance();

        // Before playing: N combos tracked, none has spawned yet (so no live children),
        // but each still has a step pending its spawn time => counted as "playing".
        {
            const auto stats = dispatcher.query(vfxsequence::GetVFXComboStatsQuery{});
            CHECK(stats.activeCombos == static_cast<uint32_t>(kCombos));
            CHECK(stats.playingCombos == static_cast<uint32_t>(kCombos));
            CHECK(stats.liveChildInstances == 0);
            CHECK(stats.culledSpawns == 0);
            CHECK(stats.pooledReuses == 0);
        }

        // Play + advance: each combo spawns its single child (kept alive by the mock).
        for (const auto id : combos)
            svc.playCombo(id);
        svc.update(0.1f);

        {
            const auto stats = dispatcher.query(vfxsequence::GetVFXComboStatsQuery{});
            CHECK(stats.activeCombos == static_cast<uint32_t>(kCombos));
            CHECK(stats.playingCombos == static_cast<uint32_t>(kCombos));
            CHECK(stats.liveChildInstances == static_cast<uint32_t>(kCombos));
            CHECK(stats.culledSpawns == 0);
        }
    }
}
