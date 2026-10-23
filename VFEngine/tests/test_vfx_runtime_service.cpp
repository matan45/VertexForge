#include <doctest.h>
#include <ostream> // doctest stringifies std::string_view through operator<<

#include <impl/vfx/VFXRuntimeServiceImpl.hpp>
#include <providers/vfx/IVFXRuntimeProvider.hpp>
#include <events/EventDispatcher.hpp>
#include <events/editor/EditorModeEvents.hpp>
#include <events/vfx/VFXRuntimeEvents.hpp>
#include <events/vfx/VFXSnapshotEvents.hpp>
#include <data/EditorMode.hpp>
#include <asset/AssetDatabase.hpp>
#include <vfx/VFXRuntimeDiagnostics.hpp>

#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

// VK-1657 — VFXRuntimeServiceImpl:
//   - effect paths (VFX::spawnAt, channels, plugins) resolve against the asset database root, and a
//     missing file returns 0 without reaching the provider instead of becoming the default effect;
//   - the Play->Edit purge of everything spawned during Play is only flagged by the mode
//     notification and runs from purgeTransientIfPending() (the editor's edit-mode VFX task), once;
//     a purge still pending at Edit->Play runs there, before Play entry.

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

    // Records what reaches the provider; everything else is a no-op.
    struct RecordingVFXProvider : services::IVFXRuntimeProvider
    {
        std::vector<services::VFXRuntimeParams> created;
        std::vector<std::string> channels;
        int purgeCalls = 0;
        std::vector<services::VFXInstanceId> transientIds{11, 12};
        size_t count = 0;

        services::VFXInstanceId createInstance(const services::VFXRuntimeParams& params) override
        {
            created.push_back(params);
            return static_cast<services::VFXInstanceId>(100 + created.size());
        }
        services::VFXInstanceId createChannel(const std::string& path, uint32_t) override
        {
            channels.push_back(path);
            return 200;
        }
        std::vector<services::VFXInstanceId> destroyTransientInstances() override
        {
            ++purgeCalls;
            return transientIds;
        }
        size_t getInstanceCount() const override { return count; }

        void init(vk::Format, vk::Format) override {}
        void cleanUp() override {}
        void recreate(vk::Format, vk::Format) override {}
        bool isInitialized() const override { return true; }
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

    // VFXRuntimeServiceImpl never unregisters its handlers, so a test that registers them must
    // remove every one (they capture the service) before the service goes out of scope.
    void unregisterRuntimeServiceHandlers()
    {
        namespace rt = services::events::vfxruntime;
        auto& d = ::events::EventDispatcher::instance();
        d.unregisterCommandHandler<rt::CreateVFXInstanceCommand>();
        d.unregisterCommandHandler<rt::CreateVFXChannelCommand>();
        d.unregisterCommandHandler<rt::EmitToVFXChannelCommand>();
        d.unregisterCommandHandler<rt::DestroyVFXInstanceCommand>();
        d.unregisterCommandHandler<rt::SetVFXInstanceTransformCommand>();
        d.unregisterCommandHandler<rt::AttachVFXInstanceToSocketCommand>();
        d.unregisterCommandHandler<rt::DetachVFXInstanceCommand>();
        d.unregisterCommandHandler<rt::ApplyVFXInstanceOverridesCommand>();
        d.unregisterCommandHandler<rt::PlayVFXInstanceCommand>();
        d.unregisterCommandHandler<rt::StopVFXInstanceCommand>();
        d.unregisterCommandHandler<rt::ResetVFXInstanceCommand>();
        d.unregisterCommandHandler<rt::UpdateVFXRuntimeCommand>();
        d.unregisterQueryHandler<rt::IsVFXInstancePlayingQuery>();
        d.unregisterQueryHandler<rt::GetVFXBudgetStatsQuery>();
        d.unregisterQueryHandler<rt::GetVFXLODConfigQuery>();
        d.unregisterCommandHandler<rt::SetVFXLODConfigCommand>();
        d.unregisterQueryHandler<rt::GetVFXCullStateQuery>();
        d.unregisterCommandHandler<rt::SetVFXQualityTierCommand>();
        d.unregisterQueryHandler<rt::GetVFXQualityTierQuery>();
        d.unregisterCommandHandler<rt::SetVFXSignificanceBudgetCommand>();
        d.unregisterQueryHandler<rt::GetVFXInstanceDebugQuery>();
        d.unregisterQueryHandler<rt::GetVFXRecentWarningsQuery>();
        d.unregisterQueryHandler<::events::vfx::snapshot::CaptureVFXSnapshotQuery>();
        d.unregisterCommandHandler<::events::vfx::snapshot::SeekVFXInstanceCommand>();
    }

    void publishMode(EditorMode previous, EditorMode current)
    {
        ::events::editor::EditorModeChangedNotification n;
        n.previousMode = previous;
        n.currentMode = current;
        ::events::EventDispatcher::instance().publish(n);
    }

    // A temporary project with assets/vfx/sparks.vfVFX, seeded as the asset database root.
    struct TempProject
    {
        fs::path root;

        explicit TempProject(const char* name)
            : root(fs::temp_directory_path() / name)
        {
            std::error_code ec;
            fs::remove_all(root, ec);
            fs::create_directories(root / "assets" / "vfx");
            std::ofstream(root / "assets" / "vfx" / "sparks.vfVFX") << "{}";
            REQUIRE(asset::AssetDatabase::instance().rebuildFromMetaFiles(root.string()));
        }

        ~TempProject()
        {
            std::error_code ec;
            fs::remove_all(root, ec);
            asset::AssetDatabase::instance().clear();
        }
    };
}

TEST_SUITE("VFXRuntimeService")
{
    TEST_CASE("a project-relative effect path resolves against the asset database root")
    {
        TempProject project("vf_vfx_runtime_relative_paths");
        RecordingVFXProvider provider;
        services::VFXRuntimeServiceImpl svc(&provider);

        const std::string expected = asset::AssetDatabase::instance().resolveAssetPath("assets/vfx/sparks.vfVFX");
        REQUIRE(fs::path(expected).is_absolute());

        services::VFXRuntimeParams params;
        params.vfxAssetPath = "assets/vfx/sparks.vfVFX";
        CHECK(svc.createInstance(params) != 0);
        REQUIRE(provider.created.size() == 1);
        CHECK(provider.created[0].vfxAssetPath == expected);

        CHECK(svc.createChannel("assets/vfx/sparks.vfVFX", 0) == 200);
        REQUIRE(provider.channels.size() == 1);
        CHECK(provider.channels[0] == expected);

        // An absolute path passes through (idempotent resolution), served from the existence cache.
        params.vfxAssetPath = expected;
        CHECK(svc.createInstance(params) != 0);
        REQUIRE(provider.created.size() == 2);
        CHECK(provider.created[1].vfxAssetPath == expected);
    }

    TEST_CASE("a missing effect returns 0 and never reaches the provider")
    {
        TempProject project("vf_vfx_runtime_missing_paths");
        vfx::VFXRuntimeDiagnostics::instance().clear();
        RecordingVFXProvider provider;
        services::VFXRuntimeServiceImpl svc(&provider);

        services::VFXRuntimeParams params;
        params.vfxAssetPath = "assets/vfx/does_not_exist.vfVFX";
        CHECK(svc.createInstance(params) == 0);
        CHECK(svc.createChannel("assets/vfx/does_not_exist.vfVFX", 0) == 0);
        CHECK(provider.created.empty());
        CHECK(provider.channels.empty());
    }

    TEST_CASE("an empty path keeps the default effect and the persistent flag passes through")
    {
        RecordingVFXProvider provider;
        services::VFXRuntimeServiceImpl svc(&provider);

        services::VFXRuntimeParams params;
        params.persistent = true;
        CHECK(svc.createInstance(params) != 0);
        REQUIRE(provider.created.size() == 1);
        CHECK(provider.created[0].vfxAssetPath.empty());
        CHECK(provider.created[0].persistent);

        // An empty channel path is still refused.
        CHECK(svc.createChannel("", 0) == 0);
        CHECK(provider.channels.empty());
    }

    TEST_CASE("the Play->Edit purge runs once, deferred to the edit-mode tick or the next Edit->Play")
    {
        RecordingVFXProvider provider;
        services::VFXRuntimeServiceImpl svc(&provider);
        svc.registerEventHandlers();
        auto cleanup = makeScopeExit([] { unregisterRuntimeServiceHandlers(); });

        svc.purgeTransientIfPending();
        CHECK(provider.purgeCalls == 0); // nothing pending

        publishMode(EditorMode::Play, EditorMode::Edit);
        CHECK(provider.purgeCalls == 0); // the notification only flags it
        svc.purgeTransientIfPending();
        CHECK(provider.purgeCalls == 1);
        svc.purgeTransientIfPending();
        CHECK(provider.purgeCalls == 1); // consumed

        // Stop and Play in back-to-back frames: the pending purge runs on Edit->Play, before Play entry,
        // so the first session's effects never reach the second; nothing is left pending after it.
        publishMode(EditorMode::Play, EditorMode::Edit);
        publishMode(EditorMode::Edit, EditorMode::Play);
        CHECK(provider.purgeCalls == 2);
        svc.purgeTransientIfPending();
        CHECK(provider.purgeCalls == 2);

        // Edit->Play without a pending purge does nothing.
        publishMode(EditorMode::Edit, EditorMode::Play);
        CHECK(provider.purgeCalls == 2);
    }

    TEST_CASE("instanceCount forwards the provider count")
    {
        RecordingVFXProvider provider;
        services::VFXRuntimeServiceImpl svc(&provider);
        provider.count = 3;
        CHECK(svc.instanceCount() == 3);

        services::VFXRuntimeServiceImpl withoutProvider(nullptr);
        CHECK(withoutProvider.instanceCount() == 0);
        withoutProvider.purgeTransientIfPending(); // no provider: a no-op
    }
}
