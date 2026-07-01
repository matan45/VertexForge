#include <doctest.h>

#include <impl/vfx/VFXPlayModeHandler.hpp>
#include <impl/vfx/VFXSequencePlayModeHandler.hpp>
#include <impl/vfx/VFXSequenceRuntimeServiceImpl.hpp>
#include <providers/vfx/IVFXRuntimeProvider.hpp>
#include <events/EventDispatcher.hpp>
#include <events/editor/EditorModeEvents.hpp>
#include <events/project/ResourceEvents.hpp>
#include <events/scene/ScenePersistenceEvents.hpp>
#include <events/vfx/VFXRuntimeEvents.hpp>
#include <events/vfx/VFXSequenceRuntimeEvents.hpp>
#include <asset/AssetDatabase.hpp>
#include <asset/AssetGUID.hpp>
#include <asset/AssetRef.hpp>
#include <components/Components.hpp>
#include <data/EntityConversion.hpp>
#include <scene/Entity.hpp>
#include <scene/EntityRegistry.hpp>
#include <vfx/VFXSequenceAsset.hpp>
#include <vfx/VFXSequenceTypes.hpp>

#include <vulkan/vulkan.hpp>
#include <algorithm>
#include <filesystem>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace
{
    namespace fs = std::filesystem;
    using EditorMode = services::EditorMode;

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

    fs::path runtimeTestRoot()
    {
        return fs::temp_directory_path() / "vf_vfx_sequence_runtime_tests";
    }

    asset::AssetRef makeResolvingRef(uint64_t guidValue, const std::string& virtualPath)
    {
        auto guid = asset::AssetGUID::fromValue(guidValue);
        asset::AssetDatabase::instance().registerAssetWithGUID(
            guid, virtualPath, resource::AssetType::VFX);
        return asset::AssetRef::fromGUID(guid);
    }

    std::string saveSequence(const std::string& fileName, const vfx::VFXSequenceData& data)
    {
        std::error_code ec;
        fs::create_directories(runtimeTestRoot(), ec);
        fs::path path = runtimeTestRoot() / fileName;
        REQUIRE(vfx::VFXSequenceAsset::save(data, path.string()));
        return path.string();
    }

    vfx::VFXSequenceData singleStepSequence(const asset::AssetRef& ref, float startTime = 0.0f)
    {
        vfx::VFXSequenceData data;
        data.name = "single";
        vfx::VFXSequenceStep step;
        step.vfxRef = ref;
        step.startTime = startTime;
        data.steps.push_back(step);
        return data;
    }

    void enterPlay()
    {
        ::events::editor::EditorModeChangedNotification n;
        n.previousMode = EditorMode::Edit;
        n.currentMode = EditorMode::Play;
        ::events::EventDispatcher::instance().publish(n);
    }

    void unregisterVFXRuntimeHandlers()
    {
        auto& d = ::events::EventDispatcher::instance();
        d.unregisterCommandHandler<services::events::vfxruntime::CreateVFXInstanceCommand>();
        d.unregisterCommandHandler<services::events::vfxruntime::DestroyVFXInstanceCommand>();
        d.unregisterCommandHandler<services::events::vfxruntime::SetVFXInstanceTransformCommand>();
        d.unregisterCommandHandler<services::events::vfxruntime::AttachVFXInstanceToSocketCommand>();
        d.unregisterCommandHandler<services::events::vfxruntime::DetachVFXInstanceCommand>();
        d.unregisterCommandHandler<services::events::vfxruntime::ApplyVFXInstanceOverridesCommand>();
        d.unregisterCommandHandler<services::events::vfxruntime::PlayVFXInstanceCommand>();
        d.unregisterCommandHandler<services::events::vfxruntime::StopVFXInstanceCommand>();
        d.unregisterCommandHandler<services::events::vfxruntime::ResetVFXInstanceCommand>();
        d.unregisterCommandHandler<services::events::vfxruntime::UpdateVFXRuntimeCommand>();
        d.unregisterQueryHandler<services::events::vfxruntime::IsVFXInstancePlayingQuery>();
    }

    void unregisterVFXSequenceHandlers()
    {
        auto& d = ::events::EventDispatcher::instance();
        d.unregisterCommandHandler<services::events::vfxsequence::CreateVFXComboInstanceCommand>();
        d.unregisterCommandHandler<services::events::vfxsequence::DestroyVFXComboInstanceCommand>();
        d.unregisterCommandHandler<services::events::vfxsequence::PlayVFXComboInstanceCommand>();
        d.unregisterCommandHandler<services::events::vfxsequence::StopVFXComboInstanceCommand>();
        d.unregisterCommandHandler<services::events::vfxsequence::ResetVFXComboInstanceCommand>();
        d.unregisterCommandHandler<services::events::vfxsequence::SetVFXComboInstanceTransformCommand>();
        d.unregisterCommandHandler<services::events::vfxsequence::AttachVFXComboInstanceToSocketCommand>();
        d.unregisterCommandHandler<services::events::vfxsequence::DetachVFXComboInstanceCommand>();
        d.unregisterCommandHandler<services::events::vfxsequence::TriggerVFXComboCueCommand>();
        d.unregisterCommandHandler<services::events::vfxsequence::UpdateVFXSequenceRuntimeCommand>();
        d.unregisterQueryHandler<services::events::vfxsequence::IsVFXComboInstancePlayingQuery>();
    }

    struct MockVFXRuntime
    {
        services::VFXInstanceId nextId = 1000;
        std::vector<std::string> createPaths;
        std::vector<services::VFXInstanceId> plays;
        std::set<services::VFXInstanceId> live;

        void install()
        {
            auto& d = ::events::EventDispatcher::instance();
            d.registerCommandHandler<services::events::vfxruntime::CreateVFXInstanceCommand>(
                [this](const services::events::vfxruntime::CreateVFXInstanceCommand& c)
                    -> services::VFXInstanceId
                {
                    const services::VFXInstanceId id = nextId++;
                    createPaths.push_back(c.params.vfxAssetPath);
                    live.insert(id);
                    return id;
                });
            d.registerCommandHandler<services::events::vfxruntime::DestroyVFXInstanceCommand>(
                [this](const services::events::vfxruntime::DestroyVFXInstanceCommand& c)
                {
                    live.erase(c.instanceId);
                });
            d.registerCommandHandler<services::events::vfxruntime::SetVFXInstanceTransformCommand>(
                [](const services::events::vfxruntime::SetVFXInstanceTransformCommand&) {});
            d.registerCommandHandler<services::events::vfxruntime::AttachVFXInstanceToSocketCommand>(
                [](const services::events::vfxruntime::AttachVFXInstanceToSocketCommand&) {});
            d.registerCommandHandler<services::events::vfxruntime::DetachVFXInstanceCommand>(
                [](const services::events::vfxruntime::DetachVFXInstanceCommand&) {});
            d.registerCommandHandler<services::events::vfxruntime::ApplyVFXInstanceOverridesCommand>(
                [](const services::events::vfxruntime::ApplyVFXInstanceOverridesCommand&) {});
            d.registerCommandHandler<services::events::vfxruntime::PlayVFXInstanceCommand>(
                [this](const services::events::vfxruntime::PlayVFXInstanceCommand& c)
                {
                    plays.push_back(c.instanceId);
                });
            d.registerCommandHandler<services::events::vfxruntime::StopVFXInstanceCommand>(
                [](const services::events::vfxruntime::StopVFXInstanceCommand&) {});
            d.registerCommandHandler<services::events::vfxruntime::ResetVFXInstanceCommand>(
                [](const services::events::vfxruntime::ResetVFXInstanceCommand&) {});
            d.registerCommandHandler<services::events::vfxruntime::UpdateVFXRuntimeCommand>(
                [](const services::events::vfxruntime::UpdateVFXRuntimeCommand&) {});
            d.registerQueryHandler<services::events::vfxruntime::IsVFXInstancePlayingQuery>(
                [this](const services::events::vfxruntime::IsVFXInstancePlayingQuery& q) -> bool
                {
                    return live.count(q.instanceId) > 0;
                });
        }
    };

    struct StubVFXProvider : services::IVFXRuntimeProvider
    {
        void init(vk::Format, vk::Format) override {}
        void cleanUp() override {}
        void recreate(vk::Format, vk::Format) override {}
        bool isInitialized() const override { return true; }
        services::VFXInstanceId createInstance(const services::VFXRuntimeParams&) override { return 0; }
        void destroyInstance(services::VFXInstanceId) override {}
        void applyInstanceOverrides(services::VFXInstanceId, const services::VFXEmitterOverrides&) override {}
        void setInstanceTransform(services::VFXInstanceId, const glm::mat4&) override {}
        void playInstance(services::VFXInstanceId) override {}
        void stopInstance(services::VFXInstanceId) override {}
        void resetInstance(services::VFXInstanceId) override {}
        bool isInstancePlaying(services::VFXInstanceId) const override { return false; }
        void update(float) override {}
        void setCamera(const services::VFXCameraParams&) override {}
        void setSceneDepthImageView(vk::ImageView) override {}
        void recordComputeCommands(const vk::CommandBuffer&) override {}
        void recordDrawCommands(const vk::CommandBuffer&) override {}
        bool hasDistortionEmitters() const override { return false; }
        void recordDistortionDrawCommands(const vk::CommandBuffer&) override {}
        void initDistortion(vk::Format, vk::Format) override {}
        void recreateDistortion(vk::Format, vk::Format) override {}
        size_t getInstanceCount() const override { return 0; }
        std::optional<PlaybackState> capturePlaybackState(services::VFXInstanceId) const override { return std::nullopt; }
        void seekInstance(services::VFXInstanceId, float, float) override {}
        void setLightingLayouts(vk::DescriptorSetLayout, vk::DescriptorSetLayout, vk::DescriptorSetLayout) override {}
        void updateLightingDescriptorSets(vk::DescriptorSet, vk::DescriptorSet, vk::DescriptorSet) override {}
        void setDistanceCullingEnabled(bool) override {}
        void setMaxDrawDistance(float) override {}
        std::vector<VFXProxyLight> getActiveProxyLights() const override { return {}; }
        BudgetStats getBudgetStats() const override { return {}; }
        LODConfig getLODConfig() const override { return {}; }
        void setLODConfig(const LODConfig&) override {}
    };
}

TEST_SUITE("VFXSequenceRuntimeFoundation")
{
    TEST_CASE("dispatcher-registered sequence runtime creates and plays a combo")
    {
        asset::AssetDatabase::instance().clear();
        MockVFXRuntime mock;
        mock.install();
        auto runtimeCleanup = makeScopeExit(unregisterVFXRuntimeHandlers);

        const auto ref = makeResolvingRef(0xA001, "assets/vfx/registered.vfVFX");
        const std::string sequencePath = saveSequence("Registered.vfVFXSequence", singleStepSequence(ref));

        services::VFXSequenceRuntimeServiceImpl svc;
        svc.registerEventHandlers();
        auto sequenceCleanup = makeScopeExit(unregisterVFXSequenceHandlers);

        services::events::vfxsequence::CreateVFXComboInstanceCommand createCmd;
        createCmd.sequenceAssetPath = sequencePath;
        createCmd.autoDestroyOnFinish = false;
        const services::VFXComboInstanceId combo =
            ::events::EventDispatcher::instance().execute(createCmd);
        REQUIRE(combo != 0);

        services::events::vfxsequence::PlayVFXComboInstanceCommand playCmd;
        playCmd.comboId = combo;
        ::events::EventDispatcher::instance().execute(playCmd);

        services::events::vfxsequence::IsVFXComboInstancePlayingQuery playingQuery;
        playingQuery.comboId = combo;
        CHECK(::events::EventDispatcher::instance().query(playingQuery));
    }

    TEST_CASE("AssetSaved invalidates positive and negative sequence cache entries")
    {
        asset::AssetDatabase::instance().clear();
        MockVFXRuntime mock;
        mock.install();
        auto runtimeCleanup = makeScopeExit(unregisterVFXRuntimeHandlers);

        const auto refA = makeResolvingRef(0xA101, "assets/vfx/cache_a.vfVFX");
        const auto refB = makeResolvingRef(0xA102, "assets/vfx/cache_b.vfVFX");
        const auto refOldLate = makeResolvingRef(0xA103, "assets/vfx/cache_old_late.vfVFX");

        vfx::VFXSequenceData v1;
        v1.name = "v1";
        vfx::VFXSequenceStep early;
        early.vfxRef = refA;
        early.startTime = 0.0f;
        v1.steps.push_back(early);
        vfx::VFXSequenceStep late;
        late.vfxRef = refOldLate;
        late.startTime = 10.0f;
        v1.steps.push_back(late);
        const std::string sequencePath = saveSequence("Cache.vfVFXSequence", v1);

        services::VFXSequenceRuntimeServiceImpl svc;
        svc.registerEventHandlers();
        auto sequenceCleanup = makeScopeExit(unregisterVFXSequenceHandlers);

        const auto combo1 = svc.createCombo(sequencePath, glm::mat4(1.0f), 0, false);
        REQUIRE(combo1 != 0);
        svc.playCombo(combo1);
        svc.update(0.1f);
        REQUIRE(mock.createPaths.size() == 1);
        CHECK(mock.createPaths.back() == "assets/vfx/cache_a.vfVFX");

        vfx::VFXSequenceData v2 = singleStepSequence(refB);
        REQUIRE(vfx::VFXSequenceAsset::save(v2, sequencePath));
        ::events::resource::AssetSavedNotification saved;
        saved.filePath = sequencePath;
        ::events::EventDispatcher::instance().publish(saved);

        // VK-1460: AssetSaved now QUEUES the invalidation; update() applies it on the update
        // thread so sequenceCache/childCache are never mutated concurrently with spawnStep
        // reads. Pump one update to flush the pending invalidation before the next createCombo
        // reads the cache (in the real runtime an update() always runs between save and spawn).
        svc.update(0.0f);

        const auto combo2 = svc.createCombo(sequencePath, glm::mat4(1.0f), 0, false);
        REQUIRE(combo2 != 0);
        svc.playCombo(combo2);
        svc.update(0.1f);
        CHECK(std::find(mock.createPaths.begin(), mock.createPaths.end(),
                        "assets/vfx/cache_b.vfVFX") != mock.createPaths.end());

        svc.update(10.0f);
        CHECK(std::find(mock.createPaths.begin(), mock.createPaths.end(),
                        "assets/vfx/cache_old_late.vfVFX") != mock.createPaths.end());

        const std::string missingPath = (runtimeTestRoot() / "MissingThenFixed.vfVFXSequence").string();
        // runtimeTestRoot() persists across runs and this test writes missingPath below
        // (line ~305). Delete it up front so the negative-cache precondition ("file does
        // not exist yet") holds on every run, not just the first.
        std::error_code missingEc;
        fs::remove(missingPath, missingEc);
        CHECK(svc.createCombo(missingPath, glm::mat4(1.0f), 0, false) == 0);
        REQUIRE(vfx::VFXSequenceAsset::save(singleStepSequence(refA), missingPath));
        saved.filePath = missingPath;
        ::events::EventDispatcher::instance().publish(saved);
        svc.update(0.0f); // VK-1460: flush the deferred negative-cache invalidation
        CHECK(svc.createCombo(missingPath, glm::mat4(1.0f), 0, false) != 0);
    }

    TEST_CASE("SceneLoaded rescan catches startup entities and stays idempotent")
    {
        asset::AssetDatabase::instance().clear();
        auto& dispatcher = ::events::EventDispatcher::instance();

        std::vector<services::VFXComboInstanceId> comboCreates;
        std::vector<services::VFXInstanceId> vfxCreates;
        services::VFXComboInstanceId nextComboId = 2000;
        services::VFXInstanceId nextVFXId = 3000;

        dispatcher.registerCommandHandler<services::events::vfxsequence::CreateVFXComboInstanceCommand>(
            [&](const services::events::vfxsequence::CreateVFXComboInstanceCommand&)
                -> services::VFXComboInstanceId
            {
                const auto id = nextComboId++;
                comboCreates.push_back(id);
                return id;
            });
        dispatcher.registerCommandHandler<services::events::vfxsequence::PlayVFXComboInstanceCommand>(
            [](const services::events::vfxsequence::PlayVFXComboInstanceCommand&) {});
        dispatcher.registerCommandHandler<services::events::vfxsequence::DestroyVFXComboInstanceCommand>(
            [](const services::events::vfxsequence::DestroyVFXComboInstanceCommand&) {});
        dispatcher.registerCommandHandler<services::events::vfxsequence::UpdateVFXSequenceRuntimeCommand>(
            [](const services::events::vfxsequence::UpdateVFXSequenceRuntimeCommand&) {});
        dispatcher.registerQueryHandler<services::events::vfxsequence::IsVFXComboInstancePlayingQuery>(
            [](const services::events::vfxsequence::IsVFXComboInstancePlayingQuery&) -> bool { return true; });
        auto sequenceCleanup = makeScopeExit(unregisterVFXSequenceHandlers);

        dispatcher.registerCommandHandler<services::events::vfxruntime::CreateVFXInstanceCommand>(
            [&](const services::events::vfxruntime::CreateVFXInstanceCommand&)
                -> services::VFXInstanceId
            {
                const auto id = nextVFXId++;
                vfxCreates.push_back(id);
                return id;
            });
        dispatcher.registerCommandHandler<services::events::vfxruntime::PlayVFXInstanceCommand>(
            [](const services::events::vfxruntime::PlayVFXInstanceCommand&) {});
        dispatcher.registerCommandHandler<services::events::vfxruntime::DestroyVFXInstanceCommand>(
            [](const services::events::vfxruntime::DestroyVFXInstanceCommand&) {});
        dispatcher.registerCommandHandler<services::events::vfxruntime::UpdateVFXRuntimeCommand>(
            [](const services::events::vfxruntime::UpdateVFXRuntimeCommand&) {});
        auto runtimeCleanup = makeScopeExit(unregisterVFXRuntimeHandlers);

        StubVFXProvider provider;
        services::VFXSequencePlayModeHandler sequenceHandler;
        services::VFXPlayModeHandler vfxHandler(&provider);
        sequenceHandler.subscribeToEvents();
        vfxHandler.subscribeToEvents();
        auto handlerCleanup = makeScopeExit([&]
        {
            sequenceHandler.unsubscribeFromEvents();
            vfxHandler.unsubscribeFromEvents();
        });

        enterPlay();
        const size_t baseComboCreates = comboCreates.size();
        const size_t baseVFXCreates = vfxCreates.size();

        scene::Entity entity("RuntimeVFXSceneLoaded");
        auto& sequence = entity.addComponent<components::VFXSequenceComponent>();
        sequence.autoPlay = true;
        sequence.sequenceRef = asset::AssetRef::fromGUID(
            asset::AssetDatabase::instance().registerAsset(
                "tests/runtime_sequence.vfVFXSequence", resource::AssetType::VFXSequence));
        auto& vfx = entity.addComponent<components::VFXComponent>();
        vfx.autoPlay = true;
        vfx.vfxRef = asset::AssetRef::fromGUID(
            asset::AssetDatabase::instance().registerAsset(
                "tests/runtime_vfx.vfVFX", resource::AssetType::VFX));
        entity.addComponent<components::WorldTransformComponent>();

        ::events::scene::SceneLoadedNotification loaded;
        loaded.scenePath = "tests/runtime_scene.vfScene";
        dispatcher.publish(loaded);

        sequenceHandler.update(0.016f);
        vfxHandler.update(0.016f);
        sequenceHandler.update(0.016f);
        vfxHandler.update(0.016f);
        sequenceHandler.update(0.016f);
        vfxHandler.update(0.016f);

        CHECK(comboCreates.size() == baseComboCreates + 1);
        CHECK(vfxCreates.size() == baseVFXCreates + 1);

        scene::EntityRegistry::getRegistry().destroy(entity.getHandle());
    }
}
