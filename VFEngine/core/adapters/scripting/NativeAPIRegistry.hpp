#pragma once

#include "../../services/data/EntityHandle.hpp"

namespace services
{
    class ScriptInterpreter;
}

namespace core
{
    class NativeAPIRegistry
    {
    private:
        ::services::ScriptInterpreter* interpreter;

        // Current context for native callbacks
        inline static ::services::EntityHandle currentCallbackEntity = ::services::EntityHandle::invalid();
        inline static uint64_t currentInstanceId = 0;

    public:
        explicit NativeAPIRegistry(::services::ScriptInterpreter* interp);
        ~NativeAPIRegistry() = default;

        NativeAPIRegistry(const NativeAPIRegistry&) = delete;
        NativeAPIRegistry& operator=(const NativeAPIRegistry&) = delete;

        void registerEngineAPIs();

        static void setCurrentEntity(const ::services::EntityHandle& entity);
        static ::services::EntityHandle getCurrentEntity();

        static void setCurrentInstanceId(uint64_t id);
        static uint64_t getCurrentInstanceId();

        // Called at the start of each frame to reset rate limiters
        static void beginFrame();
    };

    // RAII scope for the ambient script-call context (current entity + current
    // instance id). Event bridges fire synchronously off EventDispatcher
    // notifications, which can arrive in the middle of another script's native
    // call — a plain set-and-leave would clobber the outer call's context for
    // the rest of that call. Every dispatch into a script instance should sit
    // inside one of these so natives that read getCurrentEntity()/
    // getCurrentInstanceId() (Entity::self, coroutines, scriptEvent_listen)
    // see the right instance and the outer context is restored on exit.
    class AmbientScriptContext
    {
    public:
        AmbientScriptContext(const ::services::EntityHandle& entity, uint64_t instanceId)
            : savedEntity(NativeAPIRegistry::getCurrentEntity())
            , savedInstanceId(NativeAPIRegistry::getCurrentInstanceId())
        {
            NativeAPIRegistry::setCurrentEntity(entity);
            NativeAPIRegistry::setCurrentInstanceId(instanceId);
        }

        ~AmbientScriptContext()
        {
            NativeAPIRegistry::setCurrentEntity(savedEntity);
            NativeAPIRegistry::setCurrentInstanceId(savedInstanceId);
        }

        AmbientScriptContext(const AmbientScriptContext&) = delete;
        AmbientScriptContext& operator=(const AmbientScriptContext&) = delete;

    private:
        ::services::EntityHandle savedEntity;
        uint64_t savedInstanceId;
    };
}
