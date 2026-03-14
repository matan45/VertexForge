#include "RuntimeBootstrap.hpp"
#include "../../adapters/render/OffScreenAdapter.hpp"
#include "../../adapters/audio/AudioAdapter.hpp"
#include "../../adapters/scripting/ScriptingAdapter.hpp"
#include "../../adapters/physics/PhysicsAdapter.hpp"
#include "../../adapters/navmesh/NavmeshAdapter.hpp"
#include "../../adapters/physics/SocketAdapter.hpp"
#include "../../adapters/animation/AnimatorAdapter.hpp"
#include "../../adapters/physics/IKAdapter.hpp"
#include "../../adapters/vfx/VFXRuntimeAdapter.hpp"
#include "../../adapters/render/BillboardRenderAdapter.hpp"
#include "../../adapters/render/DecalRenderAdapter.hpp"
#include "../../adapters/render/PostProcessAdapter.hpp"
#include "../../adapters/render/RenderTextureAdapter.hpp"
#include "../../adapters/render/DebugDrawAdapter.hpp"
#include "../../adapters/terrain/TerrainRenderAdapter.hpp"
#include "../../adapters/ai/BehaviorTreeAdapter.hpp"

namespace core
{
    services::IOffScreenProvider* RuntimeBootstrap::getOffScreenProvider()
    {
        return offScreenAdapter.get();
    }

    services::IAudioProvider* RuntimeBootstrap::getAudioProvider()
    {
        return audioAdapter.get();
    }

    services::IScriptingProvider* RuntimeBootstrap::getScriptingProvider()
    {
        return scriptingAdapter.get();
    }

    services::IPhysicsProvider* RuntimeBootstrap::getPhysicsProvider()
    {
        return physicsAdapter.get();
    }

    services::INavmeshProvider* RuntimeBootstrap::getNavmeshProvider()
    {
        return navmeshAdapter.get();
    }

    services::ISocketProvider* RuntimeBootstrap::getSocketProvider()
    {
        return socketAdapter.get();
    }

    services::IAnimatorProvider* RuntimeBootstrap::getAnimatorProvider()
    {
        return animatorAdapter.get();
    }

    services::IIKProvider* RuntimeBootstrap::getIKProvider()
    {
        return ikAdapter.get();
    }

    services::IVFXRuntimeProvider* RuntimeBootstrap::getVFXRuntimeProvider()
    {
        return vfxRuntimeAdapter.get();
    }

    services::IPostProcessProvider* RuntimeBootstrap::getPostProcessProvider()
    {
        return postProcessAdapter.get();
    }

    services::IRenderTextureProvider* RuntimeBootstrap::getRenderTextureProvider()
    {
        return renderTextureAdapter.get();
    }

    services::IDebugDrawProvider* RuntimeBootstrap::getDebugDrawProvider()
    {
        return debugDrawAdapter.get();
    }

    TerrainRenderAdapter* RuntimeBootstrap::getTerrainRenderAdapterInternal()
    {
        return terrainRenderAdapter.get();
    }

    WaterRenderAdapter* RuntimeBootstrap::getWaterRenderAdapterInternal()
    {
        return waterRenderAdapter.get();
    }

    services::IBillboardRenderProvider* RuntimeBootstrap::getBillboardRenderProvider()
    {
        return billboardRenderAdapter.get();
    }

    services::IDecalRenderProvider* RuntimeBootstrap::getDecalRenderProvider()
    {
        return decalRenderAdapter.get();
    }

    services::IBehaviorTreeProvider* RuntimeBootstrap::getBehaviorTreeProvider()
    {
        return behaviorTreeAdapter.get();
    }
}
