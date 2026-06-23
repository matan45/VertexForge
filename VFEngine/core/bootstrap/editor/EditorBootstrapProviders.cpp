#include "EditorBootstrap.hpp"
#include "../../adapters/render/OffScreenAdapter.hpp"
#include "../../adapters/render/EditorTextureAdapter.hpp"
#include "../../adapters/render/MaterialPreviewAdapter.hpp"
#include "../../adapters/render/MeshPreviewAdapter.hpp"
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
#include "../../adapters/terrain/TerrainRaycastAdapter.hpp"
#include "../../adapters/terrain/TerrainBrushComputeAdapter.hpp"
#include "../../adapters/render/PostProcessAdapter.hpp"
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

namespace core
{
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

    services::IPrefabRigPreviewProvider* EditorBootstrap::getPrefabRigPreviewProvider()
    {
        return prefabRigPreviewAdapter.get();
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

    services::IRenderHookProvider* EditorBootstrap::getRenderHookProvider()
    {
        return renderHookAdapter.get();
    }

    services::ICustomPipelineProvider* EditorBootstrap::getCustomPipelineProvider()
    {
        return customPipelineAdapter.get();
    }

    services::IPostProcessEffectProvider* EditorBootstrap::getPostProcessEffectProvider()
    {
        return postProcessEffectAdapter.get();
    }

    services::IPluginTextureProvider* EditorBootstrap::getPluginTextureProvider()
    {
        return pluginTextureAdapter.get();
    }

    services::IDebugDrawProvider* EditorBootstrap::getDebugDrawProvider()
    {
        return debugDrawAdapter.get();
    }

    OceanRenderAdapter* EditorBootstrap::getOceanRenderAdapterInternal()
    {
        return oceanRenderAdapter.get();
    }

    services::IPostProcessProvider* EditorBootstrap::getPostProcessProvider()
    {
        return postProcessAdapter.get();
    }

    services::IGrassRenderProvider* EditorBootstrap::getGrassRenderProvider()
    {
        return grassRenderAdapter.get();
    }

    services::IBillboardRenderProvider* EditorBootstrap::getBillboardRenderProvider()
    {
        return billboardRenderAdapter.get();
    }

    services::IDecalRenderProvider* EditorBootstrap::getDecalRenderProvider()
    {
        return decalRenderAdapter.get();
    }

    services::ILightStreamingProvider* EditorBootstrap::getLightStreamingProvider()
    {
        return lightStreamingAdapter.get();
    }

    services::IObjectStreamingProvider* EditorBootstrap::getObjectStreamingProvider()
    {
        return objectStreamingAdapter.get();
    }

    services::IGIProvider* EditorBootstrap::getGIProvider()
    {
        return giAdapter.get();
    }

    services::IBehaviorTreeProvider* EditorBootstrap::getBehaviorTreeProvider()
    {
        return behaviorTreeAdapter.get();
    }

    services::IRuntimePickerProvider* EditorBootstrap::getRuntimePickerProvider()
    {
        return runtimePickerAdapter.get();
    }
}
