#include "RuntimeBootstrap.hpp"
#include "../../controllers/CoreInterface.hpp"
#include "../../controllers/OffScreen.hpp"
#include "../../adapters/audio/AudioAdapter.hpp"
#include "../../adapters/scripting/ScriptingAdapter.hpp"
#include "../../adapters/physics/PhysicsAdapter.hpp"
#include "../../adapters/navmesh/NavmeshAdapter.hpp"
#include "../../adapters/render/OffScreenAdapter.hpp"
#include "../../adapters/physics/SocketAdapter.hpp"
#include "../../adapters/animation/AnimatorAdapter.hpp"
#include "../../adapters/physics/IKAdapter.hpp"
#include "../../adapters/vfx/VFXRuntimeAdapter.hpp"
#include "../../adapters/render/PostProcessAdapter.hpp"
#include "../../adapters/terrain/TerrainRenderAdapter.hpp"
#include "../../adapters/terrain/OceanRenderAdapter.hpp"
#include "../../adapters/render/RenderTextureAdapter.hpp"
#include "../../adapters/render/DebugDrawAdapter.hpp"
#include "../../adapters/render/PluginTextureAdapter.hpp"
#include "../../adapters/render/PipelineWarmupAdapter.hpp"

namespace core
{
    void RuntimeBootstrap::cleanUp()
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

        pipelineWarmupAdapter.reset();
        pluginTextureAdapter.reset();
        renderTextureAdapter.reset();
        debugDrawAdapter.reset();
        postProcessAdapter.reset();
        terrainRenderAdapter.reset();
        oceanRenderAdapter.reset();
        vfxRuntimeAdapter.reset();
        offScreenAdapter.reset();
        audioAdapter.reset();
        scriptingAdapter.reset();
        physicsAdapter.reset();
        navmeshAdapter.reset();
        socketAdapter.reset();
        animatorAdapter.reset();
        ikAdapter.reset();

        // Destroy the offscreen render chain (holds a Device&) before the Device
        // itself is torn down in coreInterface->cleanUp(). Otherwise the chain
        // lingers as a member until ~RuntimeBootstrap and its destructors re-run
        // cleanup() against an already-destroyed Device (intermittent crash on close).
        offScreen.reset();

        if (coreInterface)
        {
            coreInterface->cleanUp();
        }
    }
}
