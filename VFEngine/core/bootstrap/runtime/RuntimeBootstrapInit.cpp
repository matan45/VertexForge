#include "RuntimeBootstrap.hpp"
#include "../../controllers/CoreInterface.hpp"
#include "../../controllers/OffScreen.hpp"
#include "../../adapters/render/OffScreenAdapter.hpp"
#include "../../adapters/audio/AudioAdapter.hpp"
#include "../../adapters/scripting/ScriptingAdapter.hpp"
#include "../../adapters/physics/PhysicsAdapter.hpp"
#include "../../adapters/navmesh/NavmeshAdapter.hpp"
#include "../../adapters/physics/SocketAdapter.hpp"
#include "../../adapters/animation/AnimatorAdapter.hpp"
#include "../../adapters/physics/IKAdapter.hpp"
#include "../../adapters/vfx/VFXRuntimeAdapter.hpp"
#include "../../adapters/render/PostProcessAdapter.hpp"
#include "../../adapters/terrain/WaterRenderAdapter.hpp"
#include "../../adapters/render/RenderTextureAdapter.hpp"
#include "../../adapters/render/DebugDrawAdapter.hpp"

namespace core
{
    RuntimeBootstrap::RuntimeBootstrap()
        : coreInterface(std::make_unique<::controllers::CoreInterface>(false))
          , offScreen(std::make_unique<::controllers::OffScreen>())
    {
    }

    RuntimeBootstrap::~RuntimeBootstrap() = default;

    void RuntimeBootstrap::init()
    {
        coreInterface->init();

        offScreenAdapter = std::make_unique<OffScreenAdapter>(offScreen.get());
        audioAdapter = std::make_unique<AudioAdapter>();
        scriptingAdapter = std::make_unique<ScriptingAdapter>();
        physicsAdapter = std::make_unique<PhysicsAdapter>();
        navmeshAdapter = std::make_unique<NavmeshAdapter>();
        socketAdapter = std::make_unique<SocketAdapter>();
        animatorAdapter = std::make_unique<AnimatorAdapter>();
        ikAdapter = std::make_unique<IKAdapter>();
        vfxRuntimeAdapter = std::make_unique<VFXRuntimeAdapter>();
        postProcessAdapter = std::make_unique<PostProcessAdapter>(offScreen.get());
        waterRenderAdapter = std::make_unique<WaterRenderAdapter>();
        renderTextureAdapter = std::make_unique<RenderTextureAdapter>(offScreen.get());
        debugDrawAdapter = std::make_unique<DebugDrawAdapter>();

        offScreen->init();

        // Disable editor-only visual aids in runtime
        offScreen->setShowGrid(false);
        offScreen->setShowDebugRendering(false);
        offScreen->setShowBillboardIcons(false);

        audioAdapter->init();
        scriptingAdapter->init();
        physicsAdapter->init();
        navmeshAdapter->init();

        // Wire VFX runtime provider to offscreen renderer
        // This allows VFX to be rendered as part of the scene
        offScreenAdapter->setVFXRuntimeProvider(vfxRuntimeAdapter.get());

        // Wire water render provider to offscreen renderer
        offScreenAdapter->setWaterRenderProvider(waterRenderAdapter.get());

        // Set up resize callback to recreate offscreen resources (Hi-Z, etc.)
        coreInterface->setResizeCallback([this]()
        {
            offScreen->recreate();
        });
    }
}
