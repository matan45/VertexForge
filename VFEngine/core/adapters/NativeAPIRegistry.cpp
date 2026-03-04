#include "print/Log.hpp"
#include "NativeAPIRegistry.hpp"
#include "api/LogAPI.hpp"
#include "api/EntityAPI.hpp"
#include "api/EntityComponentAPI.hpp"
#include "api/AudioAPI.hpp"
#include "api/InputAPI.hpp"
#include "api/PhysicsAPI.hpp"
#include "api/AnimatorAPI.hpp"
#include "api/VFXAPI.hpp"
#include "api/LightAPI.hpp"
#include "api/PostProcessAPI.hpp"
#include "api/UIAPI.hpp"
#include "api/WaterAPI.hpp"
#include "api/SocketAPI.hpp"
#include "api/NavmeshAPI.hpp"
#include "api/RenderTextureAPI.hpp"
#include "api/ControllerAPI.hpp"
#include "api/IKAPI.hpp"
#include "api/FootIKAPI.hpp"
#include "api/HandIKAPI.hpp"
#include "api/CameraAPI.hpp"
#include "api/DebugDrawAPI.hpp"

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

        vfLogInfo("[NativeAPIRegistry] Registered native engine APIs");
    }

    void NativeAPIRegistry::beginFrame()
    {
        api::PhysicsAPI::beginFrame();
        api::NavmeshAPI::beginFrame();
    }
}
