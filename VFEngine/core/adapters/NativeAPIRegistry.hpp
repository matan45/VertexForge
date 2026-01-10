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

    public:
        explicit NativeAPIRegistry(::services::ScriptInterpreter* interp);
        ~NativeAPIRegistry() = default;

        NativeAPIRegistry(const NativeAPIRegistry&) = delete;
        NativeAPIRegistry& operator=(const NativeAPIRegistry&) = delete;

        void registerEngineAPIs();

        static void setCurrentEntity(const ::services::EntityHandle& entity);
        static ::services::EntityHandle getCurrentEntity();

        // Called at the start of each frame to reset rate limiters
        static void beginFrame();
    };
}
