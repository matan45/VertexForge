#include "EditorBootstrap.hpp"
#include "../controllers/CoreInterface.hpp"
#include "../controllers/OffScreen.hpp"
#include "../adapters/OffScreenAdapter.hpp"
#include "../adapters/EditorTextureAdapter.hpp"
#include "../adapters/MaterialPreviewAdapter.hpp"
#include "../adapters/MeshPreviewAdapter.hpp"
#include "../adapters/AnimationPreviewAdapter.hpp"
#include "../adapters/VFXPreviewAdapter.hpp"
#include "../adapters/VFXRuntimeAdapter.hpp"
#include "../adapters/AudioAdapter.hpp"
#include "../adapters/ScriptingAdapter.hpp"
#include "../adapters/PhysicsAdapter.hpp"
#include "../adapters/NavmeshAdapter.hpp"
#include "../adapters/AnimatorAdapter.hpp"
#include "../adapters/SocketAdapter.hpp"
#include "../adapters/IKAdapter.hpp"
#include "../adapters/TerrainRenderAdapter.hpp"
#include "../adapters/TerrainRaycastAdapter.hpp"
#include "../adapters/TerrainBrushComputeAdapter.hpp"
#include "../adapters/PostProcessAdapter.hpp"
#include "../adapters/WaterRenderAdapter.hpp"
#include "../adapters/RenderTextureAdapter.hpp"
#include "../adapters/LightBakeAdapter.hpp"
#include "../adapters/RenderHookAdapter.hpp"
#include "../adapters/DebugDrawAdapter.hpp"
#include "../adapters/NativeAPIRegistry.hpp"
#include "scene/LevelHandler.hpp"
#include "types/PhysicsTypes.hpp"

namespace core
{
    EditorBootstrap::EditorBootstrap()
        : coreInterface(std::make_unique<::controllers::CoreInterface>())
          , offScreen(std::make_unique<::controllers::OffScreen>())
    {
    }

    EditorBootstrap::~EditorBootstrap() = default;

    void EditorBootstrap::init()
    {
        coreInterface->init();

        offScreenAdapter = std::make_unique<OffScreenAdapter>(offScreen.get());
        textureAdapter = std::make_unique<EditorTextureAdapter>();
        materialPreviewAdapter = std::make_unique<MaterialPreviewAdapter>();
        meshPreviewAdapter = std::make_unique<MeshPreviewAdapter>();
        animationPreviewAdapter = std::make_unique<AnimationPreviewAdapter>();
        vfxPreviewAdapter = std::make_unique<VFXPreviewAdapter>();
        vfxRuntimeAdapter = std::make_unique<VFXRuntimeAdapter>();
        audioAdapter = std::make_unique<AudioAdapter>();
        scriptingAdapter = std::make_unique<ScriptingAdapter>();
        physicsAdapter = std::make_unique<PhysicsAdapter>();
        navmeshAdapter = std::make_unique<NavmeshAdapter>();
        animatorAdapter = std::make_unique<AnimatorAdapter>();
        socketAdapter = std::make_unique<SocketAdapter>();
        ikAdapter = std::make_unique<IKAdapter>();
        terrainRenderAdapter = std::make_unique<TerrainRenderAdapter>();
        terrainRaycastAdapter = std::make_unique<TerrainRaycastAdapter>(*offScreen);
        terrainBrushComputeAdapter = std::make_unique<TerrainBrushComputeAdapter>(*offScreen);
        postProcessAdapter = std::make_unique<PostProcessAdapter>(offScreen.get());
        waterRenderAdapter = std::make_unique<WaterRenderAdapter>();
        renderTextureAdapter = std::make_unique<RenderTextureAdapter>(offScreen.get());
        lightBakeAdapter = std::make_unique<LightBakeAdapter>();
        renderHookAdapter = std::make_unique<RenderHookAdapter>(offScreen.get());
        debugDrawAdapter = std::make_unique<DebugDrawAdapter>();

        offScreen->init();
        audioAdapter->init();
        scriptingAdapter->init();
        physicsAdapter->init();
        navmeshAdapter->init();

        // Wire VFX runtime provider to offscreen renderer
        offScreenAdapter->setVFXRuntimeProvider(vfxRuntimeAdapter.get());

        // Wire terrain render provider to offscreen renderer
        // TerrainService will be connected later via setTerrainService() in EditorHandler
        offScreenAdapter->setTerrainRenderProvider(terrainRenderAdapter.get());

        // Wire water render provider to offscreen renderer
        // WaterService will be connected later via setWaterService() in EditorHandler
        offScreenAdapter->setWaterRenderProvider(waterRenderAdapter.get());

        // Apply default physics settings on startup
        // Scene-specific settings will be loaded when a scene is loaded
        physicsAdapter->applySettings(types::PhysicsSettings::createDefault());

        coreInterface->setResizeCallback([this]()
        {
            offScreen->recreate();
        });
    }

    void EditorBootstrap::run() const
    {
        coreInterface->run();
    }

    void EditorBootstrap::cleanUp()
    {
        if (offScreen)
        {
            offScreen->cleanUp();
        }

        if (audioAdapter)
        {
            audioAdapter->cleanUp();
        }

        if (scriptingAdapter)
        {
            scriptingAdapter->cleanUp();
        }

        if (physicsAdapter)
        {
            physicsAdapter->cleanUp();
        }

        if (navmeshAdapter)
        {
            navmeshAdapter->cleanUp();
        }

        vfxRuntimeAdapter.reset();
        vfxPreviewAdapter.reset();
        animationPreviewAdapter.reset();
        meshPreviewAdapter.reset();
        materialPreviewAdapter.reset();
        textureAdapter.reset();
        offScreenAdapter.reset();
        audioAdapter.reset();
        scriptingAdapter.reset();
        physicsAdapter.reset();
        navmeshAdapter.reset();
        animatorAdapter.reset();
        socketAdapter.reset();
        ikAdapter.reset();
        postProcessAdapter.reset();
        terrainRenderAdapter.reset();
        terrainRaycastAdapter.reset();
        renderTextureAdapter.reset();
        renderHookAdapter.reset();
        debugDrawAdapter.reset();
        lightBakeAdapter.reset();
        waterRenderAdapter.reset();

        if (coreInterface)
        {
            coreInterface->cleanUp();
        }
    }

    services::IOffScreenProvider* EditorBootstrap::getOffScreenProvider()
    {
        return offScreenAdapter.get();
    }

    services::IEditorTextureProvider* EditorBootstrap::getEditorTextureProvider()
    {
        return textureAdapter.get();
    }

    services::IMaterialPreviewProvider* EditorBootstrap::getMaterialPreviewProvider()
    {
        return materialPreviewAdapter.get();
    }

    services::IMeshPreviewProvider* EditorBootstrap::getMeshPreviewProvider()
    {
        return meshPreviewAdapter.get();
    }

    services::IAnimationPreviewProvider* EditorBootstrap::getAnimationPreviewProvider()
    {
        return animationPreviewAdapter.get();
    }

    services::IVFXPreviewProvider* EditorBootstrap::getVFXPreviewProvider()
    {
        return vfxPreviewAdapter.get();
    }

    services::IVFXRuntimeProvider* EditorBootstrap::getVFXRuntimeProvider()
    {
        return vfxRuntimeAdapter.get();
    }

    services::IAudioProvider* EditorBootstrap::getAudioProvider()
    {
        return audioAdapter.get();
    }

    services::IScriptingProvider* EditorBootstrap::getScriptingProvider()
    {
        return scriptingAdapter.get();
    }

    services::IPhysicsProvider* EditorBootstrap::getPhysicsProvider()
    {
        return physicsAdapter.get();
    }

    services::INavmeshProvider* EditorBootstrap::getNavmeshProvider()
    {
        return navmeshAdapter.get();
    }

    services::IAnimatorProvider* EditorBootstrap::getAnimatorProvider()
    {
        return animatorAdapter.get();
    }

    services::ISocketProvider* EditorBootstrap::getSocketProvider()
    {
        return socketAdapter.get();
    }

    services::IIKProvider* EditorBootstrap::getIKProvider()
    {
        return ikAdapter.get();
    }

    TerrainRenderAdapter* EditorBootstrap::getTerrainRenderAdapterInternal()
    {
        return terrainRenderAdapter.get();
    }

    services::ITerrainRaycastProvider* EditorBootstrap::getTerrainRaycastProvider()
    {
        return terrainRaycastAdapter.get();
    }

    services::ITerrainBrushComputeProvider* EditorBootstrap::getTerrainBrushComputeProvider()
    {
        return terrainBrushComputeAdapter.get();
    }

    services::IRenderTextureProvider* EditorBootstrap::getRenderTextureProvider()
    {
        return renderTextureAdapter.get();
    }

    services::ILightBakeProvider* EditorBootstrap::getLightBakeProvider()
    {
        return lightBakeAdapter.get();
    }

    services::IRenderHookProvider* EditorBootstrap::getRenderHookProvider()
    {
        return renderHookAdapter.get();
    }

    services::IDebugDrawProvider* EditorBootstrap::getDebugDrawProvider()
    {
        return debugDrawAdapter.get();
    }

    WaterRenderAdapter* EditorBootstrap::getWaterRenderAdapterInternal()
    {
        return waterRenderAdapter.get();
    }

    services::IPostProcessProvider* EditorBootstrap::getPostProcessProvider()
    {
        return postProcessAdapter.get();
    }

    window::Window* EditorBootstrap::getWindow()
    {
        return coreInterface ? coreInterface->getWindow() : nullptr;
    }

    std::shared_ptr<scene::SceneGraphSystem> EditorBootstrap::getSceneGraphSystem()
    {
        auto level = scene::LevelHandler::getInstance();
        return level ? level->getSceneGraphSystem() : nullptr;
    }

    void EditorBootstrap::setFrameCallback(std::function<void()> callback)
    {
        if (coreInterface)
        {
            // Wrap the callback to also update audio and async loading each frame
            coreInterface->setFrameCallback([this, cb = std::move(callback)]()
            {
                // Reset per-frame rate limiters for script API
                NativeAPIRegistry::beginFrame();

                if (audioAdapter)
                {
                    audioAdapter->update();
                }
               
                if (meshPreviewAdapter)
                {
                    meshPreviewAdapter->processAsyncLoading();
                }
                
                if (textureAdapter)
                {
                    textureAdapter->processAsyncLoading();
                }
                if (cb)
                {
                    cb();
                }

                // Consume immediate debug draw data and forward to renderer
                if (debugDrawAdapter)
                {
                    auto drawList = debugDrawAdapter->consumeDrawList();
                    if (!drawList.empty())
                    {
                        offScreen->updateImmediateDebugDrawList(std::move(drawList));
                    }
                }
            });
        }
    }

    void EditorBootstrap::triggerResize()
    {
        if (coreInterface)
        {
            coreInterface->triggerResize();
        }
    }

}
