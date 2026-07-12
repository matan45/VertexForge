#include <doctest.h>

#include <impl/vfx/VFXSequenceRuntimeServiceImpl.hpp>
#include <events/EventDispatcher.hpp>
#include <events/vfx/VFXEventNotifications.hpp>
#include <events/vfx/VFXRuntimeEvents.hpp>
#include <events/audio/AudioEvents.hpp> // VK-1496 — mock the Sound-step audio dispatch
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
            bool poolable = false; // VK-1460 — combo children must never opt into pooling
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
                                       c.params.autoDestroy, c.params.worldTransform, c.params.seed,
                                       c.params.poolable});
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

    // -------- VK-1460: combo children never opt into the dormant instance pool --------
    TEST_CASE("spawned combo children are created non-poolable (VK-1460)")
    {
        asset::AssetDatabase::instance().clear();
        MockVFXRuntime mock;
        mock.install();

        // A non-looping (autoDestroy), no-socket, non-camera-relative step is exactly the
        // shape that USED to opt into renderer pooling. Combo children must NOT, because a
        // combo retains the child id across frames and the pool re-issues ids with no
        // generation tag (cross-combo aliasing). Assert poolable is false despite that shape.
        vfx::VFXSequenceData data;
        data.name = "nopool";
        vfx::VFXSequenceStep s;
        s.vfxRef = makeResolvingRef(0xF0D0, "assets/vfx/np.vfVFX");
        s.startTime = 0.0f;
        s.loop = false;    // autoDestroy == true
        s.socketName = ""; // no socket
        data.steps.push_back(s);

        std::string path = saveSequence("Combo_NoPool.vfVFXSequence", data);

        VFXSequenceRuntimeServiceImpl svc;
        auto combo = svc.createCombo(path, glm::mat4(1.0f), 0, false);
        svc.playCombo(combo);
        svc.update(0.1f);

        REQUIRE(mock.creates.size() == 1);
        CHECK(mock.creates[0].autoDestroy == true); // the former pooling precondition holds
        CHECK(mock.creates[0].poolable == false);   // ...yet the child is not poolable
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

    // -------- seek/prewarm cross a cue marker WITHOUT publishing (VK-1495) --------
    // The cue->script bridge (ScriptVFXEventBridge, Core) relies on this Services-layer
    // invariant: replayTo() advances the timeline into a throwaway scratch vector and
    // never calls publishCueFired, so scrub/seek/prewarm deliver no onComboCue. Forward
    // update() DOES publish the same marker (see the marker-payload cases above, e.g. :375).
    TEST_CASE("seek/prewarm replay across a cue marker does not publish a cue notification")
    {
        asset::AssetDatabase::instance().clear();
        MockVFXRuntime mock;
        mock.install();

        std::vector<services::events::vfxsequence::VFXComboCueFiredNotification> notifications;
        ::events::ScopedSubscription sub(::events::EventDispatcher::instance().subscribe<
            services::events::vfxsequence::VFXComboCueFiredNotification>(
            [&](const auto& n) { notifications.push_back(n); }));

        vfx::VFXSequenceData data;
        data.name = "seekNoPublish";
        vfx::VFXSequenceStep s;
        s.vfxRef = makeResolvingRef(0xF024, "assets/vfx/seek_signal.vfVFX");
        s.cueName = "impact";
        data.steps.push_back(s);
        vfx::VFXSequenceEventMarker marker{0.1f, "impact"};
        marker.payload.scalar = 3.0f;
        data.eventMarkers.push_back(marker);

        std::string path = saveSequence("Combo_SeekNoPublish.vfVFXSequence", data);

        SUBCASE("seekCombo crosses the 0.1s marker")
        {
            VFXSequenceRuntimeServiceImpl svc;
            auto combo = svc.createCombo(path, glm::mat4(1.0f), 0, false);
            svc.playCombo(combo);
            svc.seekCombo(combo, 0.5f); // replayTo(0.5) crosses the marker, but must not publish
            CHECK(notifications.empty());
        }

        SUBCASE("prewarm crosses the 0.1s marker")
        {
            VFXSequenceRuntimeServiceImpl svc;
            auto combo = svc.createCombo(path, glm::mat4(1.0f), 0, false,
                                         /*seed*/ 0u, /*prewarm*/ 0.5f, /*rate*/ -1.0f, /*fixedStep*/ -1.0f);
            REQUIRE(combo != 0);
            svc.playCombo(combo); // applies prewarm via replayTo — no publish
            CHECK(notifications.empty());
        }
    }

    // ============================================================
    // VK-1496 — typed step kinds (Sound + ScriptCue).
    // ============================================================

    // -------- ScriptCue: publishes its cue forward, silent on seek --------
    TEST_CASE("a ScriptCue step publishes its cue on forward update but not on seek (VK-1496)")
    {
        asset::AssetDatabase::instance().clear();
        MockVFXRuntime mock;
        mock.install();

        std::vector<services::events::vfxsequence::VFXComboCueFiredNotification> notifications;
        ::events::ScopedSubscription sub(::events::EventDispatcher::instance().subscribe<
            services::events::vfxsequence::VFXComboCueFiredNotification>(
            [&](const auto& n) { notifications.push_back(n); }));

        vfx::VFXSequenceData data;
        data.name = "scriptcue";
        vfx::VFXSequenceStep s;
        s.kind = vfx::VFXStepKind::ScriptCue;
        s.startTime = 0.1f;
        s.emitCueName = "OnPeak";
        s.cuePayload.scalar = 7.0f;
        data.steps.push_back(s);
        std::string path = saveSequence("Combo_ScriptCue.vfVFXSequence", data);

        SUBCASE("forward update crosses the fire time and publishes exactly once, spawns no child")
        {
            VFXSequenceRuntimeServiceImpl svc;
            auto combo = svc.createCombo(path, glm::mat4(1.0f), 0, false);
            svc.playCombo(combo);
            svc.update(0.2f); // elapsed 0.2 >= 0.1 => fires
            REQUIRE(notifications.size() == 1);
            CHECK(notifications[0].cueName == "OnPeak");
            REQUIRE(notifications[0].payload.scalar.has_value());
            CHECK(*notifications[0].payload.scalar == doctest::Approx(7.0f));
            CHECK(mock.creates.empty()); // ScriptCue creates no VFX child
        }

        SUBCASE("seek across the fire time publishes nothing")
        {
            VFXSequenceRuntimeServiceImpl svc;
            auto combo = svc.createCombo(path, glm::mat4(1.0f), 0, false);
            svc.playCombo(combo);
            svc.seekCombo(combo, 0.5f); // replayTo crosses 0.1 but must stay silent
            CHECK(notifications.empty());
            CHECK(mock.creates.empty());
        }
    }

    // -------- Sound: dispatches audio forward, silent on seek --------
    TEST_CASE("a Sound step dispatches audio on forward update and stays silent on seek (VK-1496)")
    {
        asset::AssetDatabase::instance().clear();
        MockVFXRuntime mock;
        mock.install();

        struct AudioRec { std::string path; glm::vec3 position{0.0f}; float volume = 0.0f; float pitch = 0.0f; bool is3D = false; };
        std::vector<AudioRec> plays;
        auto& d = ::events::EventDispatcher::instance();
        d.registerCommandHandler<::events::audio::PlaySound3DCommand>(
            [&plays](const ::events::audio::PlaySound3DCommand& c) -> services::AudioHandle {
                plays.push_back({c.path, c.position, c.params.volume, c.params.pitch, true});
                return services::AudioHandle{1};
            });
        d.registerCommandHandler<::events::audio::PlayStreamingSoundCommand>(
            [&plays](const ::events::audio::PlayStreamingSoundCommand& c) -> services::AudioHandle {
                plays.push_back({c.path, glm::vec3(0.0f), c.params.volume, c.params.pitch, false});
                return services::AudioHandle{2};
            });

        vfx::VFXSequenceData data;
        data.name = "sound";
        vfx::VFXSequenceStep s;
        s.kind = vfx::VFXStepKind::Sound;
        s.audioRef = makeResolvingRef(0xA001, "assets/audio/boom.vfAudio");
        s.startTime = 0.1f;
        s.volume = 0.5f;
        s.pitch = 1.5f;
        s.spatialized = true;
        s.localPosition = glm::vec3(2.0f, 0.0f, 0.0f);
        data.steps.push_back(s);
        std::string path = saveSequence("Combo_Sound.vfVFXSequence", data);
        const glm::mat4 parent = glm::translate(glm::mat4(1.0f), glm::vec3(10.0f, 0.0f, 0.0f));

        SUBCASE("forward update dispatches one 3D play at parent*local world position")
        {
            VFXSequenceRuntimeServiceImpl svc;
            auto combo = svc.createCombo(path, parent, 0, false);
            svc.playCombo(combo);
            svc.update(0.2f);
            REQUIRE(plays.size() == 1);
            CHECK(plays[0].is3D);
            CHECK(plays[0].path == "assets/audio/boom.vfAudio");
            CHECK(plays[0].volume == doctest::Approx(0.5f));
            CHECK(plays[0].pitch == doctest::Approx(1.5f));
            CHECK(plays[0].position.x == doctest::Approx(12.0f)); // parent(+10) * local(+2)
            CHECK(mock.creates.empty());                          // no VFX child
        }

        SUBCASE("seek across the fire time dispatches no audio")
        {
            VFXSequenceRuntimeServiceImpl svc;
            auto combo = svc.createCombo(path, parent, 0, false);
            svc.playCombo(combo);
            svc.seekCombo(combo, 0.5f);
            CHECK(plays.empty());
        }

        // Unregister so the lambdas capturing local `plays` never outlive this test.
        d.unregisterCommandHandler<::events::audio::PlaySound3DCommand>();
        d.unregisterCommandHandler<::events::audio::PlayStreamingSoundCommand>();
    }

    // -------- Sound: headless safety (no audio handler must not throw) --------
    TEST_CASE("a Sound step with no audio handler does not throw (VK-1496)")
    {
        asset::AssetDatabase::instance().clear();
        MockVFXRuntime mock;
        mock.install();

        // Guarantee absence regardless of test order.
        auto& d = ::events::EventDispatcher::instance();
        d.unregisterCommandHandler<::events::audio::PlaySound3DCommand>();
        d.unregisterCommandHandler<::events::audio::PlayStreamingSoundCommand>();

        vfx::VFXSequenceData data;
        data.name = "soundNoHandler";
        vfx::VFXSequenceStep s;
        s.kind = vfx::VFXStepKind::Sound;
        s.audioRef = makeResolvingRef(0xA002, "assets/audio/x.vfAudio");
        s.startTime = 0.0f;
        s.spatialized = false;
        data.steps.push_back(s);
        std::string path = saveSequence("Combo_SoundNoHandler.vfVFXSequence", data);

        VFXSequenceRuntimeServiceImpl svc;
        auto combo = svc.createCombo(path, glm::mat4(1.0f), 0, false);
        svc.playCombo(combo);
        // EventDispatcher::execute would throw with no handler; fireSoundStep's try/catch swallows it.
        CHECK_NOTHROW(svc.update(0.1f));
    }

    // -------- re-entrancy: destroying a combo from onComboCue is deferred --------
    TEST_CASE("destroying a combo from its own onComboCue is deferred, not applied mid-iteration (VK-1496)")
    {
        asset::AssetDatabase::instance().clear();
        MockVFXRuntime mock;
        mock.install();

        // Combo A: a ScriptCue step at t=0 whose cue destroys A synchronously.
        vfx::VFXSequenceData dataA;
        dataA.name = "reentrantA";
        {
            vfx::VFXSequenceStep s;
            s.kind = vfx::VFXStepKind::ScriptCue;
            s.startTime = 0.0f;
            s.emitCueName = "SelfDestruct";
            dataA.steps.push_back(s);
        }
        std::string pathA = saveSequence("Combo_ReentrantA.vfVFXSequence", dataA);

        // Combo B: an ordinary VFX step at t=0 that must still spawn its child this tick.
        vfx::VFXSequenceData dataB;
        dataB.name = "reentrantB";
        {
            vfx::VFXSequenceStep s;
            s.vfxRef = makeResolvingRef(0xB001, "assets/vfx/b.vfVFX");
            s.startTime = 0.0f;
            s.loop = true;
            dataB.steps.push_back(s);
        }
        std::string pathB = saveSequence("Combo_ReentrantB.vfVFXSequence", dataB);

        VFXSequenceRuntimeServiceImpl svc;
        auto comboA = svc.createCombo(pathA, glm::mat4(1.0f), 0, false);
        auto comboB = svc.createCombo(pathB, glm::mat4(1.0f), 0, false);
        svc.playCombo(comboA);
        svc.playCombo(comboB);

        ::events::ScopedSubscription sub(::events::EventDispatcher::instance().subscribe<
            services::events::vfxsequence::VFXComboCueFiredNotification>(
            [&](const auto& n) { if (n.cueName == "SelfDestruct") svc.destroyCombo(n.comboId); }));

        // A destroys itself mid-cue (deferred); the combos loop must not be corrupted, B must
        // still spawn its child, and there must be no use-after-free.
        CHECK_NOTHROW(svc.update(0.1f));
        REQUIRE(mock.creates.size() == 1);
        CHECK(mock.creates[0].path == "assets/vfx/b.vfVFX"); // B survived the loop
        // A was erased by the drained deferral; a subsequent tick is safe.
        CHECK_NOTHROW(svc.update(0.1f));
        CHECK_FALSE(svc.isComboPlaying(comboA));
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

    // -------- VK-1498: whole-sequence loop replays without a Destroy/Create storm --------
    TEST_CASE("a looping combo replays each cycle (one Create set per cycle, no Destroys) and re-fires cues")
    {
        asset::AssetDatabase::instance().clear();
        MockVFXRuntime mock;
        mock.install();

        std::vector<services::events::vfxsequence::VFXComboCueFiredNotification> notifications;
        ::events::ScopedSubscription sub(::events::EventDispatcher::instance().subscribe<
            services::events::vfxsequence::VFXComboCueFiredNotification>(
            [&](const auto& n) { notifications.push_back(n); }));

        // Two one-shot steps + a marker. One-shot children are provider-auto-destroyed (simulated
        // by erasing them from mock.live), so the combo genuinely completes each cycle and loops.
        vfx::VFXSequenceData data;
        data.name = "loopcombo";
        {
            vfx::VFXSequenceStep s0;
            s0.vfxRef = makeResolvingRef(0xF0D0, "assets/vfx/l0.vfVFX");
            s0.startTime = 0.0f;
            s0.loop = false;
            data.steps.push_back(s0);

            vfx::VFXSequenceStep s1;
            s1.vfxRef = makeResolvingRef(0xF0D1, "assets/vfx/l1.vfVFX");
            s1.startTime = 0.5f;
            s1.loop = false;
            data.steps.push_back(s1);
        }
        data.eventMarkers.push_back(vfx::VFXSequenceEventMarker{0.3f, "beat"});
        std::string path = saveSequence("Combo_Loop.vfVFXSequence", data);

        VFXSequenceRuntimeServiceImpl svc;
        // loopSequence = true (9th arg); autoDestroyOnFinish=false so the combo is never erased.
        auto combo = svc.createCombo(path, glm::mat4(1.0f), 0, /*autoDestroyOnFinish*/ false,
                                     /*seed*/ 0u, /*prewarm*/ -1.0f, /*rate*/ -1.0f, /*fixedStep*/ -1.0f,
                                     /*loopSequence*/ true);
        REQUIRE(combo != 0);
        svc.playCombo(combo);

        auto runOneCycle = [&]()
        {
            svc.update(0.1f);                        // step0 @0 spawns
            REQUIRE(!mock.creates.empty());
            mock.live.erase(mock.creates.back().id); // step0 child finishes (provider auto-destroy)
            svc.update(0.5f);                        // elapsed 0.6: step1 spawns, marker@0.3 crosses
            mock.live.erase(mock.creates.back().id); // step1 child finishes
            svc.update(0.1f);                        // reap step1 -> complete -> restart (no new create)
        };

        runOneCycle();
        CHECK(mock.creates.size() == 2);  // exactly one Create per step, once this cycle
        CHECK(notifications.size() == 1); // marker fired once this cycle
        CHECK(svc.isComboPlaying(combo)); // restarted, still playing

        runOneCycle();
        CHECK(mock.creates.size() == 4);  // +2 for the second cycle (a replay, NOT a per-frame storm)
        CHECK(notifications.size() == 2); // the marker re-fires exactly once per iteration

        // The crux: looping must never tear down + recreate. One-shot children self-destruct
        // provider-side; the loop restart destroys nothing, so no DestroyVFXInstance is ever issued.
        CHECK(mock.destroys.empty());
    }
}
