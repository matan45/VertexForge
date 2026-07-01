#include <doctest.h>

// VK-1453 (VFXSequence Phase 4) — pre-spawn gate in the combo runtime service.
// A combo step's child .vfVFX is skipped before spawning for a non-looping, non-socket,
// non-camera-relative fire-and-forget step when either (a) the asset opts into culling
// (cullEligible) and is off-screen/out-of-range under the renderer's cull state, or
// (b) the child's scalability profile is renderer-disabled at the active quality tier.
// These tests drive the service via its public API with mock CQRS handlers on the
// dispatcher (no Vulkan device / renderer) and controllable GetVFXCullState /
// GetVFXQualityTier queries.

#include <impl/vfx/VFXSequenceRuntimeServiceImpl.hpp>
#include <events/EventDispatcher.hpp>
#include <events/vfx/VFXRuntimeEvents.hpp>
#include <events/vfx/VFXSequenceRuntimeEvents.hpp>
#include <asset/AssetDatabase.hpp>
#include <asset/AssetGUID.hpp>
#include <asset/AssetRef.hpp>
#include <vfx/VFXAsset.hpp>
#include <vfx/VFXScalability.hpp>
#include <vfx/VFXSequenceAsset.hpp>
#include <vfx/VFXSequenceTypes.hpp>
#include <vfx/VFXTypes.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
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
        return fs::temp_directory_path() / "vf_vfx_pre_cull_tests";
    }

    // The combo service records params.vfxAssetPath from AssetRef::resolve(), which
    // may normalize to forward slashes, while the test's on-disk childPath uses the
    // platform separator. Compare separator-insensitively.
    std::string normPath(std::string s)
    {
        for (char& c : s)
            if (c == '\\')
                c = '/';
        return s;
    }

    // Save a child .vfVFX with a fixed local-space bounding box (center 0, extents 1) and
    // the given cull opt-in, and return its on-disk path.
    std::string saveChildVFX(const std::string& fileName, bool cullEligible)
    {
        vfx::VFXData data = vfx::VFXAsset::createDefault("precull_child");
        data.cullEligible = cullEligible;
        data.bounds.mode = vfx::VFXBoundsMode::Fixed;
        data.bounds.center = glm::vec3(0.0f);
        data.bounds.extents = glm::vec3(1.0f);

        std::error_code ec;
        fs::create_directories(testRoot(), ec);
        const fs::path path = testRoot() / fileName;
        REQUIRE(vfx::VFXAsset::save(path.string(), data));
        return path.string();
    }

    // Save a child .vfVFX whose scalability profile enables/disables rendering at every tier.
    // cullEligible is off so only the tier gate (not frustum/distance) governs the spawn.
    std::string saveScalabilityChild(const std::string& fileName, bool rendererEnabled)
    {
        vfx::VFXData data = vfx::VFXAsset::createDefault("scal_child");
        data.cullEligible = false;
        data.scalability.enabled = true;
        for (auto& level : data.scalability.levels)
            level.rendererEnabled = rendererEnabled;

        std::error_code ec;
        fs::create_directories(testRoot(), ec);
        const fs::path path = testRoot() / fileName;
        REQUIRE(vfx::VFXAsset::save(path.string(), data));
        return path.string();
    }

    // Register the child .vfVFX under a GUID pointing at its real on-disk path so the
    // step's AssetRef resolves to a loadable file.
    asset::AssetRef makeChildRef(uint64_t guidValue, const std::string& diskPath)
    {
        const auto guid = asset::AssetGUID::fromValue(guidValue);
        asset::AssetDatabase::instance().registerAssetWithGUID(guid, diskPath, resource::AssetType::VFX);
        return asset::AssetRef::fromGUID(guid);
    }

    vfx::VFXSequenceStep makeStep(const asset::AssetRef& childRef, glm::vec3 localPos, bool loop,
                                  std::string socketName)
    {
        vfx::VFXSequenceStep step;
        step.vfxRef = childRef;
        step.startTime = 0.0f;
        step.localPosition = localPos;
        step.loop = loop;
        step.socketName = std::move(socketName);
        return step;
    }

    std::string saveSequence(const std::string& fileName, const vfx::VFXSequenceData& data)
    {
        std::error_code ec;
        fs::create_directories(testRoot(), ec);
        const fs::path path = testRoot() / fileName;
        REQUIRE(vfx::VFXSequenceAsset::save(data, path.string()));
        return path.string();
    }

    // Camera at the origin looking down -Z, 45° FOV, square aspect. A point at (0,0,-10)
    // is inside; a point far to the side (x=1000) is outside the right plane.
    glm::mat4 forwardViewProj()
    {
        const glm::mat4 view = glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::mat4 proj = glm::perspective(glm::radians(45.0f), 1.0f, 0.1f, 1000.0f);
        return proj * view;
    }

    struct MockVFXRuntime
    {
        services::VFXInstanceId nextId = 5000;
        std::vector<std::string> createPaths;
        std::set<services::VFXInstanceId> live;

        void install()
        {
            auto& d = ::events::EventDispatcher::instance();
            d.registerCommandHandler<vfxruntime::CreateVFXInstanceCommand>(
                [this](const vfxruntime::CreateVFXInstanceCommand& c) -> services::VFXInstanceId
                {
                    const services::VFXInstanceId id = nextId++;
                    createPaths.push_back(c.params.vfxAssetPath);
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

    // Register a controllable GetVFXCullStateQuery handler returning a fixed cull state.
    void installCullState(const vfxruntime::VFXCullStateResult& state)
    {
        ::events::EventDispatcher::instance().registerQueryHandler<vfxruntime::GetVFXCullStateQuery>(
            [state](const vfxruntime::GetVFXCullStateQuery&) { return state; });
    }

    // Register a GetVFXQualityTierQuery handler returning a fixed tier (mimics the runtime
    // VFX service being present so the tier scalability gate is active).
    void installQualityTier(vfx::VFXQualityTier tier)
    {
        ::events::EventDispatcher::instance().registerQueryHandler<vfxruntime::GetVFXQualityTierQuery>(
            [tier](const vfxruntime::GetVFXQualityTierQuery&) { return tier; });
    }

    void unregisterAll()
    {
        auto& d = ::events::EventDispatcher::instance();
        // vfxruntime (mock)
        d.unregisterCommandHandler<vfxruntime::CreateVFXInstanceCommand>();
        d.unregisterCommandHandler<vfxruntime::DestroyVFXInstanceCommand>();
        d.unregisterCommandHandler<vfxruntime::SetVFXInstanceTransformCommand>();
        d.unregisterCommandHandler<vfxruntime::ApplyVFXInstanceOverridesCommand>();
        d.unregisterCommandHandler<vfxruntime::PlayVFXInstanceCommand>();
        d.unregisterCommandHandler<vfxruntime::StopVFXInstanceCommand>();
        d.unregisterQueryHandler<vfxruntime::IsVFXInstancePlayingQuery>();
        d.unregisterQueryHandler<vfxruntime::GetVFXCullStateQuery>();
        d.unregisterQueryHandler<vfxruntime::GetVFXQualityTierQuery>();
        // sequence service (full set registered by registerEventHandlers)
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

    uint32_t culledSpawns()
    {
        return ::events::EventDispatcher::instance()
            .query(vfxsequence::GetVFXComboStatsQuery{})
            .culledSpawns;
    }

    vfxruntime::VFXCullStateResult frustumCullState()
    {
        vfxruntime::VFXCullStateResult state;
        state.valid = true;
        state.viewProj = forwardViewProj();
        state.cameraPos = glm::vec3(0.0f);
        state.distanceCullEnabled = false;
        state.maxDrawDistance = 0.0f;
        return state;
    }
}

TEST_SUITE("VFXPreCull")
{
    TEST_CASE("cull-eligible step inside the frustum is spawned")
    {
        asset::AssetDatabase::instance().clear();
        MockVFXRuntime mock;
        mock.install();

        const std::string childPath = saveChildVFX("child_inside.vfVFX", /*cullEligible=*/true);
        const auto childRef = makeChildRef(0xC001, childPath);
        vfx::VFXSequenceData seq;
        seq.name = "inside";
        seq.steps.push_back(makeStep(childRef, glm::vec3(0.0f, 0.0f, -10.0f), /*loop=*/false, ""));
        const std::string seqPath = saveSequence("Inside.vfVFXSequence", seq);

        services::VFXSequenceRuntimeServiceImpl svc;
        svc.registerEventHandlers();
        installCullState(frustumCullState());
        auto cleanup = makeScopeExit(unregisterAll);

        const auto combo = svc.createCombo(seqPath, glm::mat4(1.0f), 0, false);
        REQUIRE(combo != 0);
        svc.playCombo(combo);
        svc.update(0.1f);

        REQUIRE(mock.createPaths.size() == 1);
        CHECK(normPath(mock.createPaths.back()) == normPath(childPath));
        CHECK(culledSpawns() == 0);
    }

    TEST_CASE("cull-eligible step outside the frustum is culled, not spawned")
    {
        asset::AssetDatabase::instance().clear();
        MockVFXRuntime mock;
        mock.install();

        const std::string childPath = saveChildVFX("child_outside.vfVFX", /*cullEligible=*/true);
        const auto childRef = makeChildRef(0xC002, childPath);
        vfx::VFXSequenceData seq;
        seq.name = "outside";
        // Far to the side of a forward-facing 45° frustum => outside the right plane.
        seq.steps.push_back(makeStep(childRef, glm::vec3(1000.0f, 0.0f, -10.0f), /*loop=*/false, ""));
        const std::string seqPath = saveSequence("Outside.vfVFXSequence", seq);

        services::VFXSequenceRuntimeServiceImpl svc;
        svc.registerEventHandlers();
        installCullState(frustumCullState());
        auto cleanup = makeScopeExit(unregisterAll);

        const auto combo = svc.createCombo(seqPath, glm::mat4(1.0f), 0, false);
        REQUIRE(combo != 0);
        svc.playCombo(combo);
        CHECK_NOTHROW(svc.update(0.1f));

        CHECK(mock.createPaths.empty());
        CHECK(culledSpawns() == 1);
    }

    TEST_CASE("cull-eligible step outside the distance limit is culled")
    {
        asset::AssetDatabase::instance().clear();
        MockVFXRuntime mock;
        mock.install();

        const std::string childPath = saveChildVFX("child_far.vfVFX", /*cullEligible=*/true);
        const auto childRef = makeChildRef(0xC003, childPath);
        vfx::VFXSequenceData seq;
        seq.name = "far";
        // Inside the frustum (0,0,-10) but 10 units away, beyond the 5-unit draw distance.
        seq.steps.push_back(makeStep(childRef, glm::vec3(0.0f, 0.0f, -10.0f), /*loop=*/false, ""));
        const std::string seqPath = saveSequence("Far.vfVFXSequence", seq);

        services::VFXSequenceRuntimeServiceImpl svc;
        svc.registerEventHandlers();
        auto state = frustumCullState();
        state.distanceCullEnabled = true;
        state.maxDrawDistance = 5.0f;
        installCullState(state);
        auto cleanup = makeScopeExit(unregisterAll);

        const auto combo = svc.createCombo(seqPath, glm::mat4(1.0f), 0, false);
        REQUIRE(combo != 0);
        svc.playCombo(combo);
        svc.update(0.1f);

        CHECK(mock.createPaths.empty());
        CHECK(culledSpawns() == 1);
    }

    TEST_CASE("looping and socket-attached steps are never culled off-screen")
    {
        asset::AssetDatabase::instance().clear();
        MockVFXRuntime mock;
        mock.install();

        const std::string childPath = saveChildVFX("child_exempt.vfVFX", /*cullEligible=*/true);
        const auto childRef = makeChildRef(0xC004, childPath);
        vfx::VFXSequenceData seq;
        seq.name = "exempt";
        // Both steps sit far off-screen but are exempt from culling: one loops, one is
        // socket-attached. entityId 0 keeps the socket step on the combo transform without
        // a socket query.
        seq.steps.push_back(makeStep(childRef, glm::vec3(1000.0f, 0.0f, -10.0f), /*loop=*/true, ""));
        seq.steps.push_back(makeStep(childRef, glm::vec3(1000.0f, 0.0f, -10.0f), /*loop=*/false, "hand"));
        const std::string seqPath = saveSequence("Exempt.vfVFXSequence", seq);

        services::VFXSequenceRuntimeServiceImpl svc;
        svc.registerEventHandlers();
        installCullState(frustumCullState());
        auto cleanup = makeScopeExit(unregisterAll);

        const auto combo = svc.createCombo(seqPath, glm::mat4(1.0f), 0, false);
        REQUIRE(combo != 0);
        svc.playCombo(combo);
        svc.update(0.1f);

        CHECK(mock.createPaths.size() == 2);
        CHECK(culledSpawns() == 0);
    }

    TEST_CASE("no cull-state handler registered => never culls, never throws")
    {
        asset::AssetDatabase::instance().clear();
        // Defensive: ensure no cull-state handler leaked in from another case/TU so the
        // "unregistered handler" precondition holds.
        ::events::EventDispatcher::instance().unregisterQueryHandler<vfxruntime::GetVFXCullStateQuery>();

        MockVFXRuntime mock;
        mock.install();

        const std::string childPath = saveChildVFX("child_no_handler.vfVFX", /*cullEligible=*/true);
        const auto childRef = makeChildRef(0xC005, childPath);
        vfx::VFXSequenceData seq;
        seq.name = "no_handler";
        seq.steps.push_back(makeStep(childRef, glm::vec3(1000.0f, 0.0f, -10.0f), /*loop=*/false, ""));
        const std::string seqPath = saveSequence("NoHandler.vfVFXSequence", seq);

        services::VFXSequenceRuntimeServiceImpl svc;
        svc.registerEventHandlers();
        auto cleanup = makeScopeExit(unregisterAll);

        const auto combo = svc.createCombo(seqPath, glm::mat4(1.0f), 0, false);
        REQUIRE(combo != 0);
        svc.playCombo(combo);
        CHECK_NOTHROW(svc.update(0.1f));

        REQUIRE(mock.createPaths.size() == 1);
        CHECK(normPath(mock.createPaths.back()) == normPath(childPath));
        CHECK(culledSpawns() == 0);
    }

    TEST_CASE("tier-disabled steps are skipped for every step kind, without a warning")
    {
        asset::AssetDatabase::instance().clear();
        MockVFXRuntime mock;
        mock.install();

        const std::string childPath = saveScalabilityChild("child_tier_off.vfVFX", /*rendererEnabled=*/false);
        const auto childRef = makeChildRef(0xC006, childPath);
        vfx::VFXSequenceData seq;
        seq.name = "tier_off";
        // The tier disable is a GLOBAL decision: it must skip a fire-and-forget step AND a
        // looping step (which is exempt from frustum culling). Both children are on-screen and
        // cull-ineligible, so only the tier gate (rendererEnabled=false) can skip them.
        seq.steps.push_back(makeStep(childRef, glm::vec3(0.0f, 0.0f, -10.0f), /*loop=*/false, ""));
        seq.steps.push_back(makeStep(childRef, glm::vec3(0.0f, 0.0f, -10.0f), /*loop=*/true, ""));
        const std::string seqPath = saveSequence("TierOff.vfVFXSequence", seq);

        services::VFXSequenceRuntimeServiceImpl svc;
        svc.registerEventHandlers();
        // Tier present but NO cull state — proves the tier gate is independent of the camera.
        installQualityTier(vfx::VFXQualityTier::High);
        auto cleanup = makeScopeExit(unregisterAll);

        const auto combo = svc.createCombo(seqPath, glm::mat4(1.0f), 0, false);
        REQUIRE(combo != 0);
        svc.playCombo(combo);
        CHECK_NOTHROW(svc.update(0.1f));

        CHECK(mock.createPaths.empty());
        CHECK(culledSpawns() == 2);
    }

    TEST_CASE("tier-enabled effect still spawns when a tier is active")
    {
        asset::AssetDatabase::instance().clear();
        MockVFXRuntime mock;
        mock.install();

        const std::string childPath = saveScalabilityChild("child_tier_on.vfVFX", /*rendererEnabled=*/true);
        const auto childRef = makeChildRef(0xC007, childPath);
        vfx::VFXSequenceData seq;
        seq.name = "tier_on";
        seq.steps.push_back(makeStep(childRef, glm::vec3(0.0f, 0.0f, -10.0f), /*loop=*/false, ""));
        const std::string seqPath = saveSequence("TierOn.vfVFXSequence", seq);

        services::VFXSequenceRuntimeServiceImpl svc;
        svc.registerEventHandlers();
        installQualityTier(vfx::VFXQualityTier::High);
        auto cleanup = makeScopeExit(unregisterAll);

        const auto combo = svc.createCombo(seqPath, glm::mat4(1.0f), 0, false);
        REQUIRE(combo != 0);
        svc.playCombo(combo);
        svc.update(0.1f);

        REQUIRE(mock.createPaths.size() == 1);
        CHECK(normPath(mock.createPaths.back()) == normPath(childPath));
        CHECK(culledSpawns() == 0);
    }
}
