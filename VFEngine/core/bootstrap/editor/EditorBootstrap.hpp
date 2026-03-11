#pragma once
#include <memory>
#include <functional>

namespace services
{
    class IOffScreenProvider;
    class IEditorTextureProvider;
    class IMaterialPreviewProvider;
    class IMeshPreviewProvider;
    class IAnimationPreviewProvider;
    class IVFXPreviewProvider;
    class IVFXRuntimeProvider;
    class IAudioProvider;
    class IScriptingProvider;
    class IPhysicsProvider;
    class INavmeshProvider;
    class IAnimatorProvider;
    class ISocketProvider;
    class IIKProvider;
    class ITerrainRenderProvider;
    class ITerrainRaycastProvider;
    class ITerrainBrushComputeProvider;
    class IPostProcessProvider;
    class IWaterRenderProvider;
    class IRenderTextureProvider;
    class IRenderHookProvider;
    class IDebugDrawProvider;
    class IGrassRenderProvider;
    class IBillboardRenderProvider;
    class ILightStreamingProvider;
    class IGIProvider;
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

namespace core
{
    class OffScreenAdapter;
    class EditorTextureAdapter;
    class MaterialPreviewAdapter;
    class MeshPreviewAdapter;
    class AnimationPreviewAdapter;
    class VFXPreviewAdapter;
    class VFXRuntimeAdapter;
    class AudioAdapter;
    class ScriptingAdapter;
    class PhysicsAdapter;
    class NavmeshAdapter;
    class AnimatorAdapter;
    class SocketAdapter;
    class IKAdapter;
    class TerrainRenderAdapter;
    class TerrainRaycastAdapter;
    class TerrainBrushComputeAdapter;
    class PostProcessAdapter;
    class WaterRenderAdapter;
    class RenderTextureAdapter;
    class RenderHookAdapter;
    class DebugDrawAdapter;

    namespace adapters
    {
        class GrassRenderAdapter;
        class BillboardRenderAdapter;
        class LightStreamingAdapter;
        class GIAdapter;
    }

    class EditorBootstrap
    {
    private:
        std::unique_ptr<::controllers::CoreInterface> coreInterface;
        std::unique_ptr<::controllers::OffScreen> offScreen;

        std::unique_ptr<OffScreenAdapter> offScreenAdapter;
        std::unique_ptr<EditorTextureAdapter> textureAdapter;
        std::unique_ptr<MaterialPreviewAdapter> materialPreviewAdapter;
        std::unique_ptr<MeshPreviewAdapter> meshPreviewAdapter;
        std::unique_ptr<AnimationPreviewAdapter> animationPreviewAdapter;
        std::unique_ptr<VFXPreviewAdapter> vfxPreviewAdapter;
        std::unique_ptr<VFXRuntimeAdapter> vfxRuntimeAdapter;
        std::unique_ptr<AudioAdapter> audioAdapter;
        std::unique_ptr<ScriptingAdapter> scriptingAdapter;
        std::unique_ptr<PhysicsAdapter> physicsAdapter;
        std::unique_ptr<NavmeshAdapter> navmeshAdapter;
        std::unique_ptr<AnimatorAdapter> animatorAdapter;
        std::unique_ptr<SocketAdapter> socketAdapter;
        std::unique_ptr<IKAdapter> ikAdapter;
        std::unique_ptr<TerrainRenderAdapter> terrainRenderAdapter;
        std::unique_ptr<TerrainRaycastAdapter> terrainRaycastAdapter;
        std::unique_ptr<TerrainBrushComputeAdapter> terrainBrushComputeAdapter;
        std::unique_ptr<PostProcessAdapter> postProcessAdapter;
        std::unique_ptr<WaterRenderAdapter> waterRenderAdapter;
        std::unique_ptr<RenderTextureAdapter> renderTextureAdapter;
        std::unique_ptr<RenderHookAdapter> renderHookAdapter;
        std::unique_ptr<DebugDrawAdapter> debugDrawAdapter;
        std::unique_ptr<adapters::GrassRenderAdapter> grassRenderAdapter;
        std::unique_ptr<adapters::BillboardRenderAdapter> billboardRenderAdapter;
        std::unique_ptr<adapters::LightStreamingAdapter> lightStreamingAdapter;
        std::unique_ptr<adapters::GIAdapter> giAdapter;
    public:
        explicit EditorBootstrap();
        ~EditorBootstrap();

        EditorBootstrap(const EditorBootstrap&) = delete;
        EditorBootstrap& operator=(const EditorBootstrap&) = delete;

        void init();

        void run() const;

        void cleanUp();

        // === Provider Accessors ===

        services::IOffScreenProvider* getOffScreenProvider();

        services::IEditorTextureProvider* getEditorTextureProvider();

        services::IMaterialPreviewProvider* getMaterialPreviewProvider();

        services::IMeshPreviewProvider* getMeshPreviewProvider();

        services::IAnimationPreviewProvider* getAnimationPreviewProvider();

        services::IVFXPreviewProvider* getVFXPreviewProvider();

        services::IVFXRuntimeProvider* getVFXRuntimeProvider();

        services::IAudioProvider* getAudioProvider();

        services::IScriptingProvider* getScriptingProvider();

        services::IPhysicsProvider* getPhysicsProvider();

        services::INavmeshProvider* getNavmeshProvider();

        services::IAnimatorProvider* getAnimatorProvider();

        services::ISocketProvider* getSocketProvider();

        services::IIKProvider* getIKProvider();

        // For late binding - allows EditorHandler to connect TerrainService
        TerrainRenderAdapter* getTerrainRenderAdapterInternal();

        services::ITerrainRaycastProvider* getTerrainRaycastProvider();

        services::ITerrainBrushComputeProvider* getTerrainBrushComputeProvider();

        services::IPostProcessProvider* getPostProcessProvider();

        services::IRenderTextureProvider* getRenderTextureProvider();

        services::IRenderHookProvider* getRenderHookProvider();

        services::IDebugDrawProvider* getDebugDrawProvider();

        // For late binding - allows EditorHandler to connect WaterService
        WaterRenderAdapter* getWaterRenderAdapterInternal();

        services::IGrassRenderProvider* getGrassRenderProvider();

        services::IBillboardRenderProvider* getBillboardRenderProvider();

        services::ILightStreamingProvider* getLightStreamingProvider();

        services::IGIProvider* getGIProvider();

        // === Other Accessors ===

        window::Window* getWindow();

        std::shared_ptr<scene::SceneGraphSystem> getSceneGraphSystem();

        // === Frame Callbacks ===

        void setFrameCallback(std::function<void()> callback);
        void triggerResize();
    };
}
