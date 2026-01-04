#pragma once
#include <memory>
#include <string>
#include <vector>
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
        static ::services::EntityHandle currentCallbackEntity;
        static float currentDeltaTime;

    public:
        explicit NativeAPIRegistry(::services::ScriptInterpreter* interp);
        ~NativeAPIRegistry() = default;

        NativeAPIRegistry(const NativeAPIRegistry&) = delete;
        NativeAPIRegistry& operator=(const NativeAPIRegistry&) = delete;

        // Register all native engine APIs with mType
        void registerEngineAPIs();

        // Context setters for script callbacks
        static void setCurrentEntity(const ::services::EntityHandle& entity);
        static void setCurrentDeltaTime(float deltaTime);
        static ::services::EntityHandle getCurrentEntity();
        static float getCurrentDeltaTime();

    private:
        // Register individual API classes
        void registerEntityClass();
        void registerLogClass();
        void registerTimeClass();
        void registerAudioClass();
        void registerInputClass();
    };
}
