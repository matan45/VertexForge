#include "NativeAPIRegistry.hpp"
#include "../api/LogAPI.hpp"
#include "../api/EntityAPI.hpp"
#include "../api/EntityComponentAPI.hpp"
#include "../api/AudioAPI.hpp"
#include "../api/InputAPI.hpp"
#include "../api/PhysicsAPI.hpp"
#include "../api/AnimatorAPI.hpp"
#include "../api/VFXAPI.hpp"
#include "../api/LightAPI.hpp"
#include "../api/PostProcessAPI.hpp"
#include "../api/UIAPI.hpp"
#include "../api/UIAnimationAPI.hpp"
#include "../api/WaterAPI.hpp"
#include "../api/SocketAPI.hpp"
#include "../api/NavmeshAPI.hpp"
#include "../api/RenderTextureAPI.hpp"
#include "../api/ControllerAPI.hpp"
#include "../api/IKAPI.hpp"
#include "../api/FootIKAPI.hpp"
#include "../api/HandIKAPI.hpp"
#include "../api/CameraAPI.hpp"
#include "../api/DebugDrawAPI.hpp"
#include "../api/BehaviorTreeAPI.hpp"
#include "../api/DecalAPI.hpp"
#include "../api/CoroutineAPI.hpp"
#include "../api/ScriptCommunicationAPI.hpp"
#include "../api/InputActionAPI.hpp"
#include "../api/InputAxisAPI.hpp"
#include "../api/InputContextAPI.hpp"
#include "../api/AtmosphereAPI.hpp"
#include "../api/CloudAPI.hpp"
#include "../api/SceneAPI.hpp"
#include "../api/StreamingAPI.hpp"

#include "print/Log.hpp"
namespace core
{
    NativeAPIRegistry::NativeAPIRegistry(::services::ScriptInterpreter* interp)
        : interpreter(interp)
    {
    }

    void NativeAPIRegistry::setCurrentEntity(const ::services::EntityHandle& entity)
    {
        currentCallbackEntity = entity;
    }

    ::services::EntityHandle NativeAPIRegistry::getCurrentEntity()
    {
        return currentCallbackEntity;
    }

    void NativeAPIRegistry::setCurrentInstanceId(uint64_t id)
    {
        currentInstanceId = id;
    }

    uint64_t NativeAPIRegistry::getCurrentInstanceId()
    {
        return currentInstanceId;
    }

    void NativeAPIRegistry::registerEngineAPIs()
    {
        api::LogAPI::registerAPI(interpreter);
        api::EntityAPI::registerAPI(interpreter);
        api::EntityComponentAPI::registerAPI(interpreter);
        api::AudioAPI::registerAPI(interpreter);
        api::InputAPI::registerAPI(interpreter);
        api::PhysicsAPI::registerAPI(interpreter);
        api::AnimatorAPI::registerAPI(interpreter);
        api::VFXAPI::registerAPI(interpreter);
        api::LightAPI::registerAPI(interpreter);
        api::PostProcessAPI::registerAPI(interpreter);
        api::UIAPI::registerAPI(interpreter);
        api::UIAnimationAPI::registerAPI(interpreter);
        api::WaterAPI::registerAPI(interpreter);
        api::SocketAPI::registerAPI(interpreter);
        api::NavmeshAPI::registerAPI(interpreter);
        api::RenderTextureAPI::registerAPI(interpreter);
        api::ControllerAPI::registerAPI(interpreter);
        api::IKAPI::registerAPI(interpreter);
        api::FootIKAPI::registerAPI(interpreter);
        api::HandIKAPI::registerAPI(interpreter);
        api::CameraAPI::registerAPI(interpreter);
        api::DebugDrawAPI::registerAPI(interpreter);
        api::BehaviorTreeAPI::registerAPI(interpreter);
        api::DecalAPI::registerAPI(interpreter);
        api::CoroutineAPI::registerAPI(interpreter);
        api::ScriptCommunicationAPI::registerAPI(interpreter);
        api::InputActionAPI::registerAPI(interpreter);
        api::InputAxisAPI::registerAPI(interpreter);
        api::InputContextAPI::registerAPI(interpreter);
        api::AtmosphereAPI::registerAPI(interpreter);
        api::CloudAPI::registerAPI(interpreter);
        api::SceneAPI::registerAPI(interpreter);
        api::StreamingAPI::registerAPI(interpreter);

        vfLogInfo("[NativeAPIRegistry] Registered native engine APIs");
    }

    void NativeAPIRegistry::beginFrame()
    {
        api::PhysicsAPI::beginFrame();
        api::NavmeshAPI::beginFrame();
    }
}
