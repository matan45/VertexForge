#include <doctest.h>

#include <impl/vfx/VFXSequenceRuntimeServiceImpl.hpp>
#include <events/EventDispatcher.hpp>
#include <events/vfx/VFXEventNotifications.hpp>
#include <events/vfx/VFXRuntimeEvents.hpp>
#include <data/VFXTypes.hpp>
#include <vfx/VFXSequenceAsset.hpp>
#include <vfx/VFXSequenceTypes.hpp>
#include <vfx/VFXComboTimeline.hpp>
#include <asset/AssetRef.hpp>
#include <asset/AssetGUID.hpp>
#include <asset/AssetDatabase.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <filesystem>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

// ============================================================
// VK-1425 (Part C): VFXSequenceRuntimeServiceImpl orchestrates "combos" over the
// existing per-instance vfxruntime command API. Its public methods are called
// directly; it forwards Create/Play/Stop/SetTransform/Destroy/Overrides/Playing
// to ::events::EventDispatcher. No real vfxruntime handler exists in the Tests
// binary, so we register MOCK handlers and assert exactly what was dispatched.
//
// Steps are authored with PATH-based vfxRef: we register a GUID->virtual-path in
// the AssetDatabase, build the ref from that GUID, save a .vfVFXSequence, and on
// load step.vfxRef.resolve() returns the registered path (no .vfVFX file needed,
// resolve() is a pure DB map lookup).
// ============================================================

namespace
{
    namespace fs = std::filesystem;
    using namespace services;

    // Records every dispatched vfxruntime command/query. A fresh instance per test
    // is wired into the EventDispatcher singleton (re-registration just replaces).
    struct MockVFXRuntime
    {
        VFXInstanceId nextId = 100;

        struct CreateRecord
        {
            VFXInstanceId id = 0;
            std::string path;
            bool loop = false;
            bool autoDestroy = false;
            glm::mat4 worldTransform{1.0f};
            uint32_t seed = 0; // VK-1451 deterministic child seed
        };

        std::vector<CreateRecord> creates;
        std::vector<VFXInstanceId> plays;
        std::vector<VFXInstanceId> stops;
        std::vector<VFXInstanceId> destroys;
        std::vector<VFXInstanceId> setTransforms;
        std::vector<VFXInstanceId> applyOverrides;
        std::vector<services::VFXEmitterOverrides> overrideRecords;

        // Ids the provider currently reports as "playing". spawnStep() pushes here;
        // a test clears an id to simulate provider auto-destroy of a finished child.
        std::set<VFXInstanceId> live;

        void install()
        {
            auto& d = ::events::EventDispatcher::instance();

            d.registerCommandHandler<services::events::vfxruntime::CreateVFXInstanceCommand>(
                [this](const services::events::vfxruntime::CreateVFXInstanceCommand& c) -> VFXInstanceId
                {
                    VFXInstanceId id = nextId++;
                    creates.push_back({id, c.params.vfxAssetPath, c.params.loop,
                                       c.params.autoDestroy, c.params.worldTransform, c.params.seed});
                    live.insert(id);
                    return id;
                });

            d.registerCommandHandler<services::events::vfxruntime::PlayVFXInstanceCommand>(
                [this](const services::events::vfxruntime::PlayVFXInstanceCommand& c) { plays.push_back(c.instanceId); });

            d.registerCommandHandler<services::events::vfxruntime::StopVFXInstanceCommand>(
                [this](const services::events::vfxruntime::StopVFXInstanceCommand& c) { stops.push_back(c.instanceId); });

            d.registerCommandHandler<services::events::vfxruntime::DestroyVFXInstanceCommand>(
                [this](const services::events::vfxruntime::DestroyVFXInstanceCommand& c)
                {
                    destroys.push_back(c.instanceId);
                    live.erase(c.instanceId);
                });

            d.registerCommandHandler<services::events::vfxruntime::SetVFXInstanceTransformCommand>(
                [this](const services::events::vfxruntime::SetVFXInstanceTransformCommand& c) { setTransforms.push_back(c.instanceId); });

            d.registerCommandHandler<services::events::vfxruntime::ApplyVFXInstanceOverridesCommand>(
                [this](const services::events::vfxruntime::ApplyVFXInstanceOverridesCommand& c)
                {
                    applyOverrides.push_back(c.instanceId);
                    overrideRecords.push_back(c.overrides);
                });

            d.registerQueryHandler<services::events::vfxruntime::IsVFXInstancePlayingQuery>(
                [this](const services::events::vfxruntime::IsVFXInstancePlayingQuery& q) -> bool { return live.count(q.instanceId) > 0; });
        }

        int countId(const std::vector<VFXInstanceId>& v, VFXInstanceId id) const
        {
            return static_cast<int>(std::count(v.begin(), v.end(), id));
        }
    };

    fs::path testRoot()
    {
        return fs::temp_directory_path() / "vf_vfx_sequence_forwarding_tests";
    }

    // Register a GUID->virtual path so the returned AssetRef resolves to `virtualPath`.
    // The path need not exist on disk; resolve() is a DB lookup.
    asset::AssetRef makeResolvingRef(uint64_t guidValue, const std::string& virtualPath)
    {
        auto guid = asset::AssetGUID::fromValue(guidValue);
        asset::AssetDatabase::instance().registerAssetWithGUID(
            guid, virtualPath, resource::AssetType::Texture);
        return asset::AssetRef::fromGUID(guid);
    }

    // Save a sequence to a temp file and return its path. The returned data steps
    // carry resolving refs so spawnStep() gets a non-empty path on load.
    std::string saveSequence(const std::string& fileName, const vfx::VFXSequenceData& data)
    {
        std::error_code ec;
        fs::create_directories(testRoot(), ec);
        fs::path p = testRoot() / fileName;
        REQUIRE(vfx::VFXSequenceAsset::save(data, p.string()));
        return p.string();
    }
}

TEST_SUITE("VFXSequenceForwarding")
{
    // -------- time-driven spawn forwards Create + Play + SetTransform --------
    TEST_CASE("playing + update spawns each elapsed step with Create/Play/SetTransform")
    {
        asset::AssetDatabase::instance().clear();
        MockVFXRuntime mock;
        mock.install();

        vfx::VFXSequenceData data;
        data.name = "combo";
        {
            vfx::VFXSequenceStep s0;
            s0.vfxRef = makeResolvingRef(0xF001, "assets/vfx/a.vfVFX");
            s0.label = "a";
            s0.startTime = 0.0f;
            s0.loop = false;
            data.steps.push_back(s0);

            vfx::VFXSequenceStep s1;
            s1.vfxRef = makeResolvingRef(0xF002, "assets/vfx/b.vfVFX");
            s1.label = "b";
            s1.startTime = 0.5f; // does not spawn until elapsed >= 0.5
            s1.loop = true;      // looping child => autoDestroy false on Create
            data.steps.push_back(s1);
        }

        std::string path = saveSequence("Combo_Spawn.vfVFXSequence", data);

        VFXSequenceRuntimeServiceImpl svc;
        const glm::mat4 parent = glm::translate(glm::mat4(1.0f), glm::vec3(10.0f, 0.0f, 0.0f));
        auto combo = svc.createCombo(path, parent, /*entityId*/ 0, /*autoDestroyOnFinish*/ false);
        REQUIRE(combo != 0);
        svc.playCombo(combo);

        // First update at 0.1s: only step 0 (startTime 0) spawns.
        svc.update(0.1f);
        REQUIRE(mock.creates.size() == 1);
        CHECK(mock.creates[0].path == "assets/vfx/a.vfVFX");
        CHECK(mock.creates[0].loop == false);
        CHECK(mock.creates[0].autoDestroy == true); // !loop
        // Each spawned child gets exactly one Play and at least one SetTransform.
        CHECK(mock.countId(mock.plays, mock.creates[0].id) == 1);
        CHECK(mock.countId(mock.setTransforms, mock.creates[0].id) >= 1);
        CHECK(mock.countId(mock.applyOverrides, mock.creates[0].id) == 1);

        // SetTransform world = parent * stepLocal. Step 0 has identity local =>
        // child transform equals the parent transform.
        {
            // Re-run a transform cascade with a known parent to verify composition.
            const glm::mat4 expected = parent; // identity local
            // The recorded SetVFXInstanceTransform carried this; verify via a fresh
            // setComboTransform + update to re-cascade and inspect the command.
            // (We assert the math indirectly below in a dedicated transform test.)
            (void)expected;
        }

        // Advance past 0.5s so step 1 spawns. elapsed after this update = 0.1+0.5=0.6.
        svc.update(0.5f);
        REQUIRE(mock.creates.size() == 2);
        CHECK(mock.creates[1].path == "assets/vfx/b.vfVFX");
        CHECK(mock.creates[1].loop == true);
        CHECK(mock.creates[1].autoDestroy == false); // looping child
        CHECK(mock.countId(mock.plays, mock.creates[1].id) == 1);
    }

    // -------- transform composition: child world = parent * stepLocal --------
    TEST_CASE("spawned child SetTransform equals parent * step local transform")
    {
        asset::AssetDatabase::instance().clear();

        // Capture the worldTransform of the Create and the first SetTransform.
        glm::mat4 createXform{0.0f};
        glm::mat4 setXform{0.0f};
        bool sawSet = false;
        VFXInstanceId childId = 0;

        auto& d = ::events::EventDispatcher::instance();
        d.registerCommandHandler<services::events::vfxruntime::CreateVFXInstanceCommand>(
            [&](const services::events::vfxruntime::CreateVFXInstanceCommand& c) -> VFXInstanceId
            { createXform = c.params.worldTransform; childId = 555; return childId; });
        d.registerCommandHandler<services::events::vfxruntime::PlayVFXInstanceCommand>(
            [&](const services::events::vfxruntime::PlayVFXInstanceCommand&) {});
        d.registerCommandHandler<services::events::vfxruntime::ApplyVFXInstanceOverridesCommand>(
            [&](const services::events::vfxruntime::ApplyVFXInstanceOverridesCommand&) {});
        d.registerCommandHandler<services::events::vfxruntime::SetVFXInstanceTransformCommand>(
            [&](const services::events::vfxruntime::SetVFXInstanceTransformCommand& c)
            { if (!sawSet) { setXform = c.worldTransform; sawSet = true; } });
        d.registerQueryHandler<services::events::vfxruntime::IsVFXInstancePlayingQuery>(
            [&](const services::events::vfxruntime::IsVFXInstancePlayingQuery&) -> bool { return true; });

        vfx::VFXSequenceData data;
        data.name = "xform";
        vfx::VFXSequenceStep s;
        s.vfxRef = makeResolvingRef(0xF010, "assets/vfx/x.vfVFX");
        s.startTime = 0.0f;
        s.localPosition = glm::vec3(2.0f, 3.0f, 4.0f);
        s.loop = false;
        data.steps.push_back(s);

        std::string path = saveSequence("Combo_Xform.vfVFXSequence", data);

        VFXSequenceRuntimeServiceImpl svc;
        const glm::mat4 parent = glm::translate(glm::mat4(1.0f), glm::vec3(10.0f, 20.0f, 30.0f));
        auto combo = svc.createCombo(path, parent, 0, false);
        svc.playCombo(combo);
        svc.update(0.1f);

        REQUIRE(sawSet);
        const glm::mat4 expected = parent * glm::translate(glm::mat4(1.0f), glm::vec3(2.0f, 3.0f, 4.0f));
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r)
                CHECK(setXform[c][r] == doctest::Approx(expected[c][r]));
        // Create used the same composed transform.
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r)
                CHECK(createXform[c][r] == doctest::Approx(expected[c][r]));
    }

    // -------- cue steps don't spawn on time; triggerCue spawns them --------
    TEST_CASE("cue-gated step does not spawn on time and spawns on triggerCue")
    {
        asset::AssetDatabase::instance().clear();
        MockVFXRuntime mock;
        mock.install();

        vfx::VFXSequenceData data;
        data.name = "cued";
        vfx::VFXSequenceStep s;
        s.vfxRef = makeResolvingRef(0xF020, "assets/vfx/cue.vfVFX");
        s.startTime = 0.0f;
        s.cueName = "impact"; // time-driven spawn is skipped while cueName set
        s.loop = false;
        data.steps.push_back(s);

        std::string path = saveSequence("Combo_Cue.vfVFXSequence", data);

        VFXSequenceRuntimeServiceImpl svc;
        auto combo = svc.createCombo(path, glm::mat4(1.0f), 0, false);
        svc.playCombo(combo);

        svc.update(1.0f); // well past startTime, but cue-gated => no spawn
        CHECK(mock.creates.empty());

        svc.triggerCue(combo, "impact");
        REQUIRE(mock.creates.size() == 1);
        CHECK(mock.creates[0].path == "assets/vfx/cue.vfVFX");

        // Triggering an unknown cue spawns nothing more.
        svc.triggerCue(combo, "nope");
        CHECK(mock.creates.size() == 1);
    }

    TEST_CASE("manual cue payload publishes notification and applies child overrides")
    {
        asset::AssetDatabase::instance().clear();
        MockVFXRuntime mock;
        mock.install();

        std::vector<services::events::vfxsequence::VFXComboCueFiredNotification> notifications;
        ::events::ScopedSubscription sub(::events::EventDispatcher::instance().subscribe<
            services::events::vfxsequence::VFXComboCueFiredNotification>(
            [&](const auto& n) { notifications.push_back(n); }));

        vfx::VFXSequenceData data;
        data.name = "payload";
        vfx::VFXSequenceStep s;
        s.vfxRef = makeResolvingRef(0xF021, "assets/vfx/payload.vfVFX");
        s.cueName = "impact";
        s.overrides.push_back(vfx::VFXParamOverride{"spawnRate", 5.0f});
        data.steps.push_back(s);

        std::string path = saveSequence("Combo_CuePayload.vfVFXSequence", data);

        VFXSequenceRuntimeServiceImpl svc;
        auto combo = svc.createCombo(path, glm::mat4(1.0f), 0, false);
        svc.playCombo(combo);

        vfx::VFXCuePayload payload;
        payload.position = glm::vec3(2.0f, 0.0f, 0.0f);
        payload.color = glm::vec4(0.2f, 0.4f, 0.6f, 1.0f);
        payload.custom.push_back(vfx::VFXParamOverride{"spawnRate", 9.0f});

        svc.triggerCue(combo, "impact", payload);

        REQUIRE(notifications.size() == 1);
        CHECK(notifications[0].comboId == combo);
        CHECK(notifications[0].cueName == "impact");
        REQUIRE(notifications[0].payload.position.has_value());
        CHECK(notifications[0].payload.position->x == doctest::Approx(2.0f));

        REQUIRE(mock.creates.size() == 1);
        CHECK(mock.creates[0].worldTransform[3][0] == doctest::Approx(2.0f));
        REQUIRE(mock.overrideRecords.size() == 1);
        REQUIRE(mock.overrideRecords[0].startColor.has_value());
        CHECK(mock.overrideRecords[0].startColor->z == doctest::Approx(0.6f));
        REQUIRE(mock.overrideRecords[0].spawnRate.has_value());
        CHECK(*mock.overrideRecords[0].spawnRate == doctest::Approx(9.0f));
    }

    TEST_CASE("marker payload publishes notification and applies to marker-spawned children")
    {
        asset::AssetDatabase::instance().clear();
        MockVFXRuntime mock;
        mock.install();

        std::vector<services::events::vfxsequence::VFXComboCueFiredNotification> notifications;
        ::events::ScopedSubscription sub(::events::EventDispatcher::instance().subscribe<
            services::events::vfxsequence::VFXComboCueFiredNotification>(
            [&](const auto& n) { notifications.push_back(n); }));

        vfx::VFXSequenceData data;
        data.name = "markerPayload";
        vfx::VFXSequenceStep s;
        s.vfxRef = makeResolvingRef(0xF022, "assets/vfx/marker_payload.vfVFX");
        s.cueName = "impact";
        data.steps.push_back(s);

        vfx::VFXSequenceEventMarker marker{0.1f, "impact"};
        marker.payload.position = glm::vec3(0.0f, 3.0f, 0.0f);
        marker.payload.color = glm::vec4(1.0f, 0.0f, 0.25f, 1.0f);
        data.eventMarkers.push_back(marker);

        std::string path = saveSequence("Combo_MarkerPayload.vfVFXSequence", data);

        VFXSequenceRuntimeServiceImpl svc;
        auto combo = svc.createCombo(path, glm::mat4(1.0f), 0, false);
        svc.playCombo(combo);
        svc.update(0.2f);

        REQUIRE(notifications.size() == 1);
        CHECK(notifications[0].cueName == "impact");
        REQUIRE(notifications[0].payload.color.has_value());
        CHECK(notifications[0].payload.color->z == doctest::Approx(0.25f));

        REQUIRE(mock.creates.size() == 1);
        CHECK(mock.creates[0].worldTransform[3][1] == doctest::Approx(3.0f));
        REQUIRE(mock.overrideRecords.size() == 1);
        REQUIRE(mock.overrideRecords[0].startColor.has_value());
        CHECK(mock.overrideRecords[0].startColor->z == doctest::Approx(0.25f));
    }

    TEST_CASE("pure-signal marker publishes cue notification without spawning a child")
    {
        asset::AssetDatabase::instance().clear();
        MockVFXRuntime mock;
        mock.install();

        std::vector<services::events::vfxsequence::VFXComboCueFiredNotification> notifications;
        ::events::ScopedSubscription sub(::events::EventDispatcher::instance().subscribe<
            services::events::vfxsequence::VFXComboCueFiredNotification>(
            [&](const auto& n) { notifications.push_back(n); }));

        vfx::VFXSequenceData data;
        data.name = "signal";
        vfx::VFXSequenceStep future;
        future.vfxRef = makeResolvingRef(0xF023, "assets/vfx/future_signal.vfVFX");
        future.startTime = 5.0f;
        data.steps.push_back(future);
        vfx::VFXSequenceEventMarker marker{0.1f, "signalOnly"};
        marker.payload.scalar = 8.0f;
        data.eventMarkers.push_back(marker);

        std::string path = saveSequence("Combo_SignalOnly.vfVFXSequence", data);

        VFXSequenceRuntimeServiceImpl svc;
        auto combo = svc.createCombo(path, glm::mat4(1.0f), 0, false);
        svc.playCombo(combo);
        svc.update(0.2f);

        CHECK(mock.creates.empty());
        REQUIRE(notifications.size() == 1);
        CHECK(notifications[0].cueName == "signalOnly");
        REQUIRE(notifications[0].payload.scalar.has_value());
        CHECK(*notifications[0].payload.scalar == doctest::Approx(8.0f));
    }

    // -------- stopCombo: Stop to non-loop, Destroy to loop children --------
    TEST_CASE("stopCombo forwards Stop to non-loop children and Destroy to loop children")
    {
        asset::AssetDatabase::instance().clear();
        MockVFXRuntime mock;
        mock.install();

        vfx::VFXSequenceData data;
        data.name = "stopmix";
        {
            vfx::VFXSequenceStep nonLoop;
            nonLoop.vfxRef = makeResolvingRef(0xF030, "assets/vfx/n.vfVFX");
            nonLoop.startTime = 0.0f;
            nonLoop.loop = false;
            data.steps.push_back(nonLoop);

            vfx::VFXSequenceStep loop;
            loop.vfxRef = makeResolvingRef(0xF031, "assets/vfx/l.vfVFX");
            loop.startTime = 0.0f;
            loop.loop = true;
            data.steps.push_back(loop);
        }

        std::string path = saveSequence("Combo_Stop.vfVFXSequence", data);

        VFXSequenceRuntimeServiceImpl svc;
        auto combo = svc.createCombo(path, glm::mat4(1.0f), 0, false);
        svc.playCombo(combo);
        svc.update(0.1f); // both spawn
        REQUIRE(mock.creates.size() == 2);

        const VFXInstanceId nonLoopId = mock.creates[0].id;
        const VFXInstanceId loopId = mock.creates[1].id;

        svc.stopCombo(combo);
        CHECK(mock.countId(mock.stops, nonLoopId) == 1);    // non-loop => Stop
        CHECK(mock.countId(mock.destroys, loopId) == 1);    // loop => Destroy
        CHECK(mock.countId(mock.stops, loopId) == 0);
        CHECK(mock.countId(mock.destroys, nonLoopId) == 0);
    }

    // -------- destroyCombo / resetCombo destroy all live children --------
    TEST_CASE("destroyCombo and resetCombo destroy every spawned child")
    {
        asset::AssetDatabase::instance().clear();

        SUBCASE("destroyCombo")
        {
            MockVFXRuntime mock;
            mock.install();

            vfx::VFXSequenceData data;
            data.name = "destroyall";
            for (int i = 0; i < 2; ++i)
            {
                vfx::VFXSequenceStep s;
                s.vfxRef = makeResolvingRef(0xF040 + i, "assets/vfx/d" + std::to_string(i) + ".vfVFX");
                s.startTime = 0.0f;
                s.loop = (i == 1); // mix loop + non-loop; destroyCombo hits both
                data.steps.push_back(s);
            }
            std::string path = saveSequence("Combo_Destroy.vfVFXSequence", data);

            VFXSequenceRuntimeServiceImpl svc;
            auto combo = svc.createCombo(path, glm::mat4(1.0f), 0, false);
            svc.playCombo(combo);
            svc.update(0.1f);
            REQUIRE(mock.creates.size() == 2);

            svc.destroyCombo(combo);
            CHECK(mock.countId(mock.destroys, mock.creates[0].id) == 1);
            CHECK(mock.countId(mock.destroys, mock.creates[1].id) == 1);
            // Combo is gone.
            CHECK(svc.isComboPlaying(combo) == false);
        }

        SUBCASE("resetCombo")
        {
            MockVFXRuntime mock;
            mock.install();

            vfx::VFXSequenceData data;
            data.name = "resetall";
            for (int i = 0; i < 2; ++i)
            {
                vfx::VFXSequenceStep s;
                s.vfxRef = makeResolvingRef(0xF050 + i, "assets/vfx/r" + std::to_string(i) + ".vfVFX");
                s.startTime = 0.0f;
                s.loop = false;
                data.steps.push_back(s);
            }
            std::string path = saveSequence("Combo_Reset.vfVFXSequence", data);

            VFXSequenceRuntimeServiceImpl svc;
            auto combo = svc.createCombo(path, glm::mat4(1.0f), 0, false);
            svc.playCombo(combo);
            svc.update(0.1f);
            REQUIRE(mock.creates.size() == 2);

            svc.resetCombo(combo);
            CHECK(mock.countId(mock.destroys, mock.creates[0].id) == 1);
            CHECK(mock.countId(mock.destroys, mock.creates[1].id) == 1);
            CHECK(svc.isComboPlaying(combo) == false);

            // After reset, replaying re-spawns the steps (spawned flags cleared).
            const size_t before = mock.creates.size();
            svc.playCombo(combo);
            svc.update(0.1f);
            CHECK(mock.creates.size() == before + 2);
        }
    }

    // -------- provider auto-destroy reaps the child; combo auto-removes --------
    TEST_CASE("auto-destroyed child is reaped and an autoDestroyOnFinish combo is removed")
    {
        asset::AssetDatabase::instance().clear();
        MockVFXRuntime mock;
        mock.install();

        vfx::VFXSequenceData data;
        data.name = "finish";
        vfx::VFXSequenceStep s;
        s.vfxRef = makeResolvingRef(0xF060, "assets/vfx/f.vfVFX");
        s.startTime = 0.0f;
        s.loop = false; // non-looping: reapable
        data.steps.push_back(s);

        std::string path = saveSequence("Combo_Finish.vfVFXSequence", data);

        VFXSequenceRuntimeServiceImpl svc;
        auto combo = svc.createCombo(path, glm::mat4(1.0f), 0, /*autoDestroyOnFinish*/ true);
        svc.playCombo(combo);

        // Spawn the only step.
        svc.update(0.1f);
        REQUIRE(mock.creates.size() == 1);
        const VFXInstanceId childId = mock.creates[0].id;
        CHECK(svc.isComboPlaying(combo) == true);

        // Simulate the provider finishing/auto-destroying the child: the playing
        // query now returns false. On the next update the step is reaped, the combo
        // finishes (playing=false) and, being autoDestroyOnFinish, is removed.
        mock.live.erase(childId);
        svc.update(0.1f);

        CHECK(svc.isComboPlaying(combo) == false); // combo gone or finished
    }

    // -------- invalid vfxRef step is skipped, no Create, no crash --------
    TEST_CASE("a step whose vfxRef does not resolve is skipped with no Create")
    {
        asset::AssetDatabase::instance().clear();
        MockVFXRuntime mock;
        mock.install();

        // One resolving step + one whose vfxRef is a VALID-but-unregistered GUID.
        // The codec keeps the step on load (the GUID is structurally valid, so it
        // passes deserializeStep's isValid() gate), but at spawn time resolve()
        // returns "" (the GUID is in no database and writeAssetRef wrote no path
        // fallback because it did not resolve at save time either). spawnStep must
        // then skip it with no Create and no crash.
        vfx::VFXSequenceData data;
        data.name = "skip";

        vfx::VFXSequenceStep good;
        good.vfxRef = makeResolvingRef(0xF070, "assets/vfx/good.vfVFX");
        good.startTime = 0.0f;
        good.loop = false;
        data.steps.push_back(good);

        vfx::VFXSequenceStep bad;
        // Valid GUID, never registered => isValid() true, resolve() empty, and no
        // path fallback is serialized (it resolves to "" at save time).
        bad.vfxRef = asset::AssetRef::fromGUID(asset::AssetGUID::fromValue(0xF071));
        REQUIRE(bad.vfxRef.isValid());
        REQUIRE(bad.vfxRef.resolve().empty());
        bad.startTime = 0.0f;
        bad.loop = false;
        data.steps.push_back(bad);

        std::string path = saveSequence("Combo_Skip.vfVFXSequence", data);

        VFXSequenceRuntimeServiceImpl svc;
        auto combo = svc.createCombo(path, glm::mat4(1.0f), 0, false);
        REQUIRE(combo != 0);
        svc.playCombo(combo);
        svc.update(0.1f); // must not crash

        // Exactly one Create — only the resolving step spawned.
        REQUIRE(mock.creates.size() == 1);
        CHECK(mock.creates[0].path == "assets/vfx/good.vfVFX");
    }

    // ============================================================
    // VK-1451 — deterministic transport at the service level
    // ============================================================

    // -------- spawned child carries the derived per-step seed --------
    TEST_CASE("spawned child seed equals deriveSeed(comboSeed, stepIndex)")
    {
        asset::AssetDatabase::instance().clear();
        MockVFXRuntime mock;
        mock.install();

        vfx::VFXSequenceData data;
        data.name = "seeded";
        data.seed = 12345u; // explicit asset seed => deterministic
        for (int i = 0; i < 2; ++i)
        {
            vfx::VFXSequenceStep s;
            s.vfxRef = makeResolvingRef(0xF080 + i, "assets/vfx/s" + std::to_string(i) + ".vfVFX");
            s.startTime = 0.0f;
            s.loop = false;
            data.steps.push_back(s);
        }
        std::string path = saveSequence("Combo_Seeded.vfVFXSequence", data);

        VFXSequenceRuntimeServiceImpl svc;
        auto combo = svc.createCombo(path, glm::mat4(1.0f), 0, false); // seed arg 0 => use asset 12345
        svc.playCombo(combo);
        svc.update(0.1f);

        REQUIRE(mock.creates.size() == 2);
        CHECK(mock.creates[0].seed == vfx::VFXComboTimeline::deriveSeed(12345u, 0));
        CHECK(mock.creates[1].seed == vfx::VFXComboTimeline::deriveSeed(12345u, 1));
        CHECK(mock.creates[0].seed != mock.creates[1].seed);
        CHECK(mock.creates[0].seed != 0u);
    }

    // -------- pause halts spawns; resume re-enables them --------
    TEST_CASE("pause stops the schedule; resume lets due steps spawn")
    {
        asset::AssetDatabase::instance().clear();
        MockVFXRuntime mock;
        mock.install();

        vfx::VFXSequenceData data;
        data.name = "paused";
        vfx::VFXSequenceStep s;
        s.vfxRef = makeResolvingRef(0xF090, "assets/vfx/p.vfVFX");
        s.startTime = 0.5f;
        s.loop = false;
        data.steps.push_back(s);
        std::string path = saveSequence("Combo_Paused.vfVFXSequence", data);

        VFXSequenceRuntimeServiceImpl svc;
        auto combo = svc.createCombo(path, glm::mat4(1.0f), 0, false);
        svc.playCombo(combo);

        svc.setComboPaused(combo, true);
        svc.update(1.0f); // would normally cross 0.5, but paused => no spawn
        CHECK(mock.creates.empty());

        svc.setComboPaused(combo, false);
        svc.update(1.0f); // now the step is due
        REQUIRE(mock.creates.size() == 1);
        CHECK(mock.creates[0].path == "assets/vfx/p.vfVFX");
    }

    // -------- playback rate scales how fast the schedule advances --------
    TEST_CASE("playback rate 2x reaches a step in half the wall-clock time")
    {
        asset::AssetDatabase::instance().clear();
        MockVFXRuntime mock;
        mock.install();

        vfx::VFXSequenceData data;
        data.name = "rate";
        data.playbackRate = 2.0f; // 2x
        vfx::VFXSequenceStep s;
        s.vfxRef = makeResolvingRef(0xF0A0, "assets/vfx/r.vfVFX");
        s.startTime = 1.0f;
        s.loop = false;
        data.steps.push_back(s);
        std::string path = saveSequence("Combo_Rate.vfVFXSequence", data);

        VFXSequenceRuntimeServiceImpl svc;
        auto combo = svc.createCombo(path, glm::mat4(1.0f), 0, false);
        svc.playCombo(combo);

        svc.update(0.6f); // scaled = 1.2 >= 1.0 => spawns (at rate 1.0 it would not)
        REQUIRE(mock.creates.size() == 1);
        CHECK(mock.creates[0].path == "assets/vfx/r.vfVFX");
    }

    // -------- seek jumps the schedule: live steps spawn, future steps don't --------
    TEST_CASE("seekCombo spawns steps live at the target time and not future steps")
    {
        asset::AssetDatabase::instance().clear();
        MockVFXRuntime mock;
        mock.install();

        vfx::VFXSequenceData data;
        data.name = "seek";
        {
            vfx::VFXSequenceStep early; // live at t=1.0 (looping, no stop window)
            early.vfxRef = makeResolvingRef(0xF0B0, "assets/vfx/early.vfVFX");
            early.startTime = 0.0f;
            early.loop = true;
            data.steps.push_back(early);

            vfx::VFXSequenceStep future; // not yet at t=1.0
            future.vfxRef = makeResolvingRef(0xF0B1, "assets/vfx/future.vfVFX");
            future.startTime = 2.0f;
            future.loop = false;
            data.steps.push_back(future);
        }
        std::string path = saveSequence("Combo_Seek.vfVFXSequence", data);

        VFXSequenceRuntimeServiceImpl svc;
        auto combo = svc.createCombo(path, glm::mat4(1.0f), 0, false);
        svc.playCombo(combo);

        svc.seekCombo(combo, 1.0f);

        // Only the early (still-live) step is (re)spawned at the seek target.
        REQUIRE(mock.creates.size() == 1);
        CHECK(mock.creates[0].path == "assets/vfx/early.vfVFX");
    }

    // -------- prewarm at create fast-forwards the schedule before the first frame --------
    TEST_CASE("a combo created with prewarm spawns due steps immediately on play")
    {
        asset::AssetDatabase::instance().clear();
        MockVFXRuntime mock;
        mock.install();

        vfx::VFXSequenceData data;
        data.name = "prewarm";
        vfx::VFXSequenceStep s;
        s.vfxRef = makeResolvingRef(0xF0C0, "assets/vfx/pw.vfVFX");
        s.startTime = 1.0f;
        s.loop = true; // stays live after spawning
        data.steps.push_back(s);
        std::string path = saveSequence("Combo_Prewarm.vfVFXSequence", data);

        VFXSequenceRuntimeServiceImpl svc;
        // prewarm 2.0s (> startTime 1.0) => the step should already be live when play starts.
        auto combo = svc.createCombo(path, glm::mat4(1.0f), 0, false,
                                     /*seed*/ 0u, /*prewarm*/ 2.0f, /*rate*/ -1.0f, /*fixedStep*/ -1.0f);
        REQUIRE(combo != 0);
        svc.playCombo(combo); // applies prewarm

        REQUIRE(mock.creates.size() == 1);
        CHECK(mock.creates[0].path == "assets/vfx/pw.vfVFX");
    }
}
