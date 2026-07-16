#include "EditorBootstrap.hpp"
#include "../../controllers/CoreInterface.hpp"
#include "../../controllers/OffScreen.hpp"
#include "../../adapters/audio/AudioAdapter.hpp"
#include "../../adapters/scripting/ScriptingAdapter.hpp"
#include "../../adapters/physics/PhysicsAdapter.hpp"
#include "../../adapters/navmesh/NavmeshAdapter.hpp"
#include "../../adapters/render/OffScreenAdapter.hpp"
#include "../../adapters/render/EditorTextureAdapter.hpp"
#include "../../adapters/render/MaterialPreviewAdapter.hpp"
#include "../../adapters/render/MeshPreviewAdapter.hpp"
#include "../../adapters/render/ThumbnailRenderAdapter.hpp"
#include "../../adapters/animation/AnimationPreviewAdapter.hpp"
#include "../../adapters/render/PrefabRigPreviewAdapter.hpp"
#include "../../adapters/render/UILayerPreviewAdapter.hpp"
#include "../../adapters/vfx/VFXPreviewAdapter.hpp"
#include "../../adapters/vfx/VFXRuntimeAdapter.hpp"
#include "../../adapters/animation/AnimatorAdapter.hpp"
#include "../../adapters/physics/SocketAdapter.hpp"
#include "../../adapters/physics/IKAdapter.hpp"
#include "../../adapters/render/PostProcessAdapter.hpp"
#include "../../adapters/terrain/TerrainRenderAdapter.hpp"
#include "../../adapters/terrain/TerrainRaycastAdapter.hpp"
#include "../../adapters/terrain/TerrainBrushComputeAdapter.hpp"
#include "../../adapters/render/RenderTextureAdapter.hpp"
#include "../../adapters/render/RenderHookAdapter.hpp"
#include "../../adapters/render/CustomPipelineAdapter.hpp"
#include "../../adapters/render/PostProcessEffectAdapter.hpp"
#include "../../adapters/render/PluginTextureAdapter.hpp"
#include "../../adapters/render/DebugDrawAdapter.hpp"
#include "../../adapters/render/PipelineWarmupAdapter.hpp"
#include "../../adapters/terrain/OceanRenderAdapter.hpp"
#include "../../adapters/vegetation/GrassRenderAdapter.hpp"

namespace core
{
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
        // Reset the prefab-rig and UI-layer preview adapters here, while the Device and the
        // Streamline interposer are still alive. Their controllers call
        // device.getLogicalDevice().waitIdle() in cleanUp()/the destructor; if they instead
        // survived to ~EditorBootstrap (after coreInterface->cleanUp() tears down the Device
        // and sl.interposer.dll) the waitIdle would jump into freed code (shutdown crash).
        prefabRigPreviewAdapter.reset();
        uiLayerPreviewAdapter.reset();
        animationPreviewAdapter.reset();
        thumbnailRenderAdapter.reset();
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
        terrainBrushComputeAdapter.reset();
        renderTextureAdapter.reset();
        renderHookAdapter.reset();
        pipelineWarmupAdapter.reset();
        customPipelineAdapter.reset();
        postProcessEffectAdapter.reset();
        pluginTextureAdapter.reset();
        debugDrawAdapter.reset();
        oceanRenderAdapter.reset();
        grassRenderAdapter.reset();

        // Destroy the offscreen render chain (holds a Device&) before the Device
        // itself is torn down in coreInterface->cleanUp(). Otherwise the chain
        // lingers as a member until ~EditorBootstrap and its destructors re-run
        // cleanup() against an already-destroyed Device (intermittent crash on close).
        offScreen.reset();

        if (coreInterface)
        {
            coreInterface->cleanUp();
        }
    }
}
