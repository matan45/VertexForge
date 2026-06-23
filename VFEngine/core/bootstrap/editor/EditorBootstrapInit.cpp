#include "EditorBootstrap.hpp"
#include "scene/EntityRegistry.hpp"
#include "../../controllers/CoreInterface.hpp"
#include "../../controllers/OffScreen.hpp"
#include "../../adapters/render/OffScreenAdapter.hpp"
#include "../../adapters/render/EditorTextureAdapter.hpp"
#include "../../adapters/render/MaterialPreviewAdapter.hpp"
#include "../../adapters/render/MeshPreviewAdapter.hpp"
#include "../../adapters/render/ThumbnailRenderAdapter.hpp"
#include "../../adapters/animation/AnimationPreviewAdapter.hpp"
#include "../../adapters/render/PrefabRigPreviewAdapter.hpp"
#include "../../adapters/vfx/VFXPreviewAdapter.hpp"
#include "../../adapters/vfx/VFXRuntimeAdapter.hpp"
#include "../../adapters/audio/AudioAdapter.hpp"
#include "../../adapters/scripting/ScriptingAdapter.hpp"
#include "../../adapters/physics/PhysicsAdapter.hpp"
#include "../../adapters/navmesh/NavmeshAdapter.hpp"
#include "../../adapters/animation/AnimatorAdapter.hpp"
#include "../../adapters/physics/SocketAdapter.hpp"
#include "../../adapters/physics/IKAdapter.hpp"
#include "../../adapters/terrain/TerrainRenderAdapter.hpp"
#include "../../adapters/terrain/TerrainRaycastAdapter.hpp"
#include "../../adapters/terrain/TerrainBrushComputeAdapter.hpp"
#include "../../adapters/render/PostProcessAdapter.hpp"
#include "../../adapters/terrain/OceanRenderAdapter.hpp"
#include "../../adapters/render/RenderTextureAdapter.hpp"
#include "../../adapters/render/RenderHookAdapter.hpp"
#include "../../adapters/render/CustomPipelineAdapter.hpp"
#include "../../adapters/render/PostProcessEffectAdapter.hpp"
#include "../../adapters/render/PluginTextureAdapter.hpp"
#include "../../adapters/render/DebugDrawAdapter.hpp"
#include "../../adapters/vegetation/GrassRenderAdapter.hpp"
#include "../../adapters/render/BillboardRenderAdapter.hpp"
#include "../../adapters/render/DecalRenderAdapter.hpp"
#include "../../adapters/render/LightStreamingAdapter.hpp"
#include "../../adapters/render/ObjectStreamingAdapter.hpp"
#include "../../adapters/render/GIAdapter.hpp"
#include "../../adapters/ai/BehaviorTreeAdapter.hpp"
#include "../../adapters/input/RuntimePickerAdapter.hpp"
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
        scene::EntityRegistry::init();
        coreInterface->init();

        offScreenAdapter = std::make_unique<OffScreenAdapter>(offScreen.get());
        textureAdapter = std::make_unique<EditorTextureAdapter>();
        materialPreviewAdapter = std::make_unique<MaterialPreviewAdapter>();
        meshPreviewAdapter = std::make_unique<MeshPreviewAdapter>();
        thumbnailRenderAdapter = std::make_unique<ThumbnailRenderAdapter>();
        animationPreviewAdapter = std::make_unique<AnimationPreviewAdapter>();
        prefabRigPreviewAdapter = std::make_unique<PrefabRigPreviewAdapter>();
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
        oceanRenderAdapter = std::make_unique<OceanRenderAdapter>();
        renderTextureAdapter = std::make_unique<RenderTextureAdapter>(offScreen.get());
        renderHookAdapter = std::make_unique<RenderHookAdapter>(offScreen.get());
        customPipelineAdapter = std::make_unique<CustomPipelineAdapter>(offScreen.get());
        postProcessEffectAdapter = std::make_unique<PostProcessEffectAdapter>(offScreen.get());
        pluginTextureAdapter = std::make_unique<PluginTextureAdapter>(offScreen.get());
        debugDrawAdapter = std::make_unique<DebugDrawAdapter>();
        grassRenderAdapter = std::make_unique<adapters::GrassRenderAdapter>();
        billboardRenderAdapter = std::make_unique<adapters::BillboardRenderAdapter>();
        decalRenderAdapter = std::make_unique<adapters::DecalRenderAdapter>();
        lightStreamingAdapter = std::make_unique<adapters::LightStreamingAdapter>();
        objectStreamingAdapter = std::make_unique<adapters::ObjectStreamingAdapter>();
        giAdapter = std::make_unique<adapters::GIAdapter>();
        behaviorTreeAdapter = std::make_unique<BehaviorTreeAdapter>(scriptingAdapter.get());
        runtimePickerAdapter = std::make_unique<RuntimePickerAdapter>();

        offScreen->init();

        // Wire adapters to offscreen controller
        billboardRenderAdapter->setOffScreenController(offScreen.get());
        decalRenderAdapter->setOffScreenController(offScreen.get());
        lightStreamingAdapter->setOffScreenController(offScreen.get());
        objectStreamingAdapter->setOffScreenController(offScreen.get());
        giAdapter->setOffScreenController(offScreen.get());
        audioAdapter->init();
        scriptingAdapter->init();
        physicsAdapter->init();
        navmeshAdapter->init();
        thumbnailRenderAdapter->init(); // registers Content Browser thumbnail render handlers

        // Wire VFX runtime provider to offscreen renderer
        offScreenAdapter->setVFXRuntimeProvider(vfxRuntimeAdapter.get());

        // Wire terrain render provider to offscreen renderer
        // TerrainService will be connected later via setTerrainService() in EditorHandler
        offScreenAdapter->setTerrainRenderProvider(terrainRenderAdapter.get());

        // Wire ocean render provider to offscreen renderer
        // OceanService will be connected later via setOceanService() in EditorHandler
        offScreenAdapter->setOceanRenderProvider(oceanRenderAdapter.get());

        // Wire vegetation render providers to offscreen renderer
        offScreenAdapter->setGrassRenderProvider(grassRenderAdapter.get());
        // Apply default physics settings on startup
        // Scene-specific settings will be loaded when a scene is loaded
        physicsAdapter->applySettings(types::PhysicsSettings::createDefault());

        coreInterface->setResizeCallback([this]()
        {
            offScreen->recreate();
        });
    }
}
