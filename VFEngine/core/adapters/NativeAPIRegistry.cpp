#include "NativeAPIRegistry.hpp"
#include "api/LogAPI.hpp"
#include "api/EntityAPI.hpp"
#include "api/AudioAPI.hpp"
#include "api/InputAPI.hpp"
#include "api/PhysicsAPI.hpp"
#include "api/AnimatorAPI.hpp"
#include "api/VFXAPI.hpp"
#include "api/LightAPI.hpp"
#include "api/PostProcessAPI.hpp"
#include "print/EditorLogger.hpp"

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
        api::AudioAPI::registerAPI(interpreter);
        api::InputAPI::registerAPI(interpreter);
        api::PhysicsAPI::registerAPI(interpreter);
        api::AnimatorAPI::registerAPI(interpreter);
        api::VFXAPI::registerAPI(interpreter);
        api::LightAPI::registerAPI(interpreter);
        api::PostProcessAPI::registerAPI(interpreter);

        vfLogInfo("[NativeAPIRegistry] Registered native engine APIs");
    }

    void NativeAPIRegistry::beginFrame()
    {
        api::PhysicsAPI::beginFrame();
    }
}
