#pragma once
#include <memory>
#include <functional>
#include <string>

namespace services
{
    class IOffScreenProvider;
    class IAudioProvider;
    class IScriptingProvider;
    class IPhysicsProvider;
    class INavmeshProvider;
    class ISocketProvider;
    class IAnimatorProvider;
    class IIKProvider;
    class IVFXRuntimeProvider;
    class IPostProcessProvider;
    class IRenderTextureProvider;
    class IDebugDrawProvider;
    class IBillboardRenderProvider;
    class IDecalRenderProvider;
    class IBehaviorTreeProvider;
    class IRuntimePickerProvider;
}

namespace window
{
    class Window;
}

namespace scene
{
    class SceneGraphSystem;
}

namespace controllers
{
    class CoreInterface;
    class OffScreen;
}

namespace types
{
    enum class PresentMode : uint8_t;
    enum class MsaaSamples : uint8_t;
}

namespace core
{
    class OffScreenAdapter;
    class AudioAdapter;
    class ScriptingAdapter;
    class PhysicsAdapter;
    class NavmeshAdapter;
    class SocketAdapter;
    class AnimatorAdapter;
    class IKAdapter;
    class VFXRuntimeAdapter;
    class PostProcessAdapter;
    class TerrainRenderAdapter;
    class OceanRenderAdapter;
    class RenderTextureAdapter;
    class DebugDrawAdapter;

    namespace adapters
    {
        class BillboardRenderAdapter;
        class DecalRenderAdapter;
    }
    class BehaviorTreeAdapter;
    class RuntimePickerAdapter;

    class RuntimeBootstrap
    {
    private:
        std::unique_ptr<::controllers::CoreInterface> coreInterface;
        std::unique_ptr<::controllers::OffScreen> offScreen;

        std::unique_ptr<OffScreenAdapter> offScreenAdapter;
        std::unique_ptr<AudioAdapter> audioAdapter;
        std::unique_ptr<ScriptingAdapter> scriptingAdapter;
        std::unique_ptr<PhysicsAdapter> physicsAdapter;
        std::unique_ptr<NavmeshAdapter> navmeshAdapter;
        std::unique_ptr<SocketAdapter> socketAdapter;
        std::unique_ptr<AnimatorAdapter> animatorAdapter;
        std::unique_ptr<IKAdapter> ikAdapter;
        std::unique_ptr<VFXRuntimeAdapter> vfxRuntimeAdapter;
        std::unique_ptr<PostProcessAdapter> postProcessAdapter;
        std::unique_ptr<TerrainRenderAdapter> terrainRenderAdapter;
        std::unique_ptr<OceanRenderAdapter> oceanRenderAdapter;
        std::unique_ptr<RenderTextureAdapter> renderTextureAdapter;
        std::unique_ptr<DebugDrawAdapter> debugDrawAdapter;
        std::unique_ptr<adapters::BillboardRenderAdapter> billboardRenderAdapter;
        std::unique_ptr<adapters::DecalRenderAdapter> decalRenderAdapter;
        std::unique_ptr<BehaviorTreeAdapter> behaviorTreeAdapter;
        std::unique_ptr<RuntimePickerAdapter> runtimePickerAdapter;

    public:
        explicit RuntimeBootstrap();
        ~RuntimeBootstrap();

        // Non-copyable
        RuntimeBootstrap(const RuntimeBootstrap&) = delete;
        RuntimeBootstrap& operator=(const RuntimeBootstrap&) = delete;

        void init();

        void run() const;

        void cleanUp();

        // === Provider Accessors ===

        services::IOffScreenProvider* getOffScreenProvider();

        services::IAudioProvider* getAudioProvider();

        services::IScriptingProvider* getScriptingProvider();

        services::IPhysicsProvider* getPhysicsProvider();

        services::INavmeshProvider* getNavmeshProvider();

        services::ISocketProvider* getSocketProvider();

        services::IAnimatorProvider* getAnimatorProvider();

        services::IIKProvider* getIKProvider();

        services::IVFXRuntimeProvider* getVFXRuntimeProvider();

        services::IPostProcessProvider* getPostProcessProvider();

        services::IRenderTextureProvider* getRenderTextureProvider();

        services::IDebugDrawProvider* getDebugDrawProvider();

        services::IBillboardRenderProvider* getBillboardRenderProvider();

        services::IDecalRenderProvider* getDecalRenderProvider();

        services::IBehaviorTreeProvider* getBehaviorTreeProvider();

        services::IRuntimePickerProvider* getRuntimePickerProvider();

        // For late binding - allows RuntimeHandler to connect services
        TerrainRenderAdapter* getTerrainRenderAdapterInternal();
        OceanRenderAdapter* getOceanRenderAdapterInternal();

        // === Other Accessors ===

        window::Window* getWindow();

        void setWindowTitle(const std::string& title);
        void setWindowIcon(const std::string& iconPath);

        std::shared_ptr<scene::SceneGraphSystem> getSceneGraphSystem();

        // === Frame Callbacks ===

        // Set callback to be called each frame (for service updates)
        void setFrameCallback(std::function<void()> callback);

        // Set callback called after scene graph update (world transforms are valid)
        void setPostUpdateCallback(std::function<void()> callback);

        // Trigger window resize handling
        void triggerResize();
        void applyDisplaySettings(types::PresentMode presentMode, types::MsaaSamples msaa);

        std::function<void()> getSceneGraphUpdateFn() const;
        std::function<void()> getRenderFn() const;
    };
}
