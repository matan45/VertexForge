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
#include "../../adapters/animation/AnimationPreviewAdapter.hpp"
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
#include "../../adapters/render/DebugDrawAdapter.hpp"
#include "../../adapters/lightbake/LightBakeAdapter.hpp"
#include "../../adapters/terrain/WaterRenderAdapter.hpp"
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
        terrainBrushComputeAdapter.reset();
        renderTextureAdapter.reset();
        renderHookAdapter.reset();
        debugDrawAdapter.reset();
        lightBakeAdapter.reset();
        waterRenderAdapter.reset();
        grassRenderAdapter.reset();

        if (coreInterface)
        {
            coreInterface->cleanUp();
        }
    }
}
