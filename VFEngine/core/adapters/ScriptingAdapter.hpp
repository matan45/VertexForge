#pragma once
#include "../../services/providers/IScriptingProvider.hpp"
#include "NativeAPIRegistry.hpp"
#include <memory>
#include <unordered_map>
#include <string>
#include <any>

namespace services
{
    class ScriptInterpreter;
}

namespace core
{
    class ScriptingAdapter : public ::services::IScriptingProvider
    {
    private:
        std::unique_ptr<::services::ScriptInterpreter> interpreter;
        std::unique_ptr<NativeAPIRegistry> apiRegistry;

        // Script instance tracking
        std::unordered_map<uint64_t, std::string> instanceToClassName; // instanceId -> class name
        std::unordered_map<uint64_t, ::services::EntityHandle> instanceToEntity; // instanceId -> entity
        std::unordered_map<uint64_t, std::any> instanceToObject; // instanceId -> script object instance (type-erased)
        std::unordered_map<std::string, std::string> pathToClassName; // scriptPath -> class name

        // Error tracking
        mutable std::optional<::services::ScriptError> lastError;

        // Instance ID generation
        uint64_t nextInstanceId = 1;

        // Script library path
        std::string scriptLibraryPath;

        // Initialization state
        bool initialized = false;

    public:
        explicit ScriptingAdapter();
        ~ScriptingAdapter() override;

        ScriptingAdapter(const ScriptingAdapter&) = delete;
        ScriptingAdapter& operator=(const ScriptingAdapter&) = delete;

        // === VM Lifecycle ===
        bool init() override;
        void cleanUp() override;
        bool isInitialized() const override;

        // === Script Loading ===
        std::optional<::services::ScriptInstanceInfo> loadScript(
            const std::string& scriptPath,
            ::services::EntityHandle entity) override;
        void unloadScript(uint64_t instanceId) override;
        bool isScriptLoaded(uint64_t instanceId) const override;

        // === Lifecycle Calls ===
        void callOnStart(uint64_t instanceId) override;
        void callOnUpdate(uint64_t instanceId, float deltaTime) override;
        void callOnDestroy(uint64_t instanceId) override;

        // === Error Handling ===
        std::optional<::services::ScriptError> getLastError() const override;
        void clearError() override;

        // === Script Library Path ===
        void setScriptLibraryPath(const std::string& path) override;

    private:
        // Helper: set error
        void setError(::services::ScriptError::Type type, const std::string& message,
                      const std::string& file = "", int line = 0);

        // Helper: extract class name from script file
        std::string extractClassName(const std::string& scriptPath);
    };
}
