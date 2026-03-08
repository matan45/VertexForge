#include "EditorBootstrap.hpp"
#include "../../controllers/CoreInterface.hpp"
#include "../../controllers/OffScreen.hpp"
#include "../../adapters/render/OffScreenAdapter.hpp"
#include "../../adapters/render/EditorTextureAdapter.hpp"
#include "../../adapters/render/MaterialPreviewAdapter.hpp"
#include "../../adapters/render/MeshPreviewAdapter.hpp"
#include "../../adapters/animation/AnimationPreviewAdapter.hpp"
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
#include "../../adapters/terrain/WaterRenderAdapter.hpp"
#include "../../adapters/render/RenderTextureAdapter.hpp"
#include "../../adapters/lightbake/LightBakeAdapter.hpp"
#include "../../adapters/render/RenderHookAdapter.hpp"
#include "../../adapters/render/DebugDrawAdapter.hpp"
#include "../../adapters/vegetation/VegetationAdapter.hpp"
#include "../../adapters/vegetation/GrassRenderAdapter.hpp"
#include "../../adapters/vegetation/VegetationRenderAdapter.hpp"
#include "../../adapters/render/BillboardRenderAdapter.hpp"
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
        vegetationAdapter = std::make_unique<adapters::VegetationAdapter>();
        grassRenderAdapter = std::make_unique<adapters::GrassRenderAdapter>();
        vegetationRenderAdapter = std::make_unique<adapters::VegetationRenderAdapter>();
        billboardRenderAdapter = std::make_unique<adapters::BillboardRenderAdapter>();

        offScreen->init();

        // Wire billboard render adapter to offscreen controller
        billboardRenderAdapter->setOffScreenController(offScreen.get());
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

        // Wire vegetation render providers to offscreen renderer
        offScreenAdapter->setGrassRenderProvider(grassRenderAdapter.get());
        offScreenAdapter->setVegetationRenderProvider(vegetationRenderAdapter.get());

        // Apply default physics settings on startup
        // Scene-specific settings will be loaded when a scene is loaded
        physicsAdapter->applySettings(types::PhysicsSettings::createDefault());

        coreInterface->setResizeCallback([this]()
        {
            offScreen->recreate();
        });
    }
}
