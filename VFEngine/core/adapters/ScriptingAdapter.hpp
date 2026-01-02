#pragma once
#include "../../services/providers/IScriptingProvider.hpp"
#include <memory>
#include <unordered_map>
#include <string>
#include <any>

// Forward declare mType's ScriptInterpreter
// Note: mType uses 'services' namespace - same name as VertexForge's but different location
namespace services {
    class ScriptInterpreter;
}

namespace core {

    class ScriptingAdapter : public ::services::IScriptingProvider {
    private:
        std::unique_ptr<::services::ScriptInterpreter> interpreter;

        // Script instance tracking
        std::unordered_map<uint64_t, std::string> instanceToClassName;  // instanceId -> class name
        std::unordered_map<uint64_t, ::services::EntityHandle> instanceToEntity;  // instanceId -> entity
        std::unordered_map<uint64_t, std::any> instanceToObject;  // instanceId -> script object instance (type-erased)
        std::unordered_map<std::string, std::string> pathToClassName;  // scriptPath -> class name

        // Error tracking
        mutable std::optional<::services::ScriptError> lastError;

        // Instance ID generation
        uint64_t nextInstanceId = 1;

        // Script library path
        std::string scriptLibraryPath;

        // Initialization state
        bool initialized = false;

        // Current context for native callbacks (thread-local in real impl)
        static ::services::EntityHandle currentCallbackEntity;
        static float currentDeltaTime;

    public:
        ScriptingAdapter();
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

        // === Property Access ===
        std::vector<::services::ScriptPropertyInfo> getProperties(uint64_t instanceId) const override;
        bool setProperty(uint64_t instanceId, const std::string& name,
                         const std::any& value) override;
        std::optional<std::any> getProperty(uint64_t instanceId,
                                             const std::string& name) const override;

        // === Method Calls ===
        std::vector<::services::ScriptMethodInfo> getMethods(uint64_t instanceId) const override;
        std::optional<std::any> callMethod(uint64_t instanceId,
                                            const std::string& methodName,
                                            const std::vector<std::any>& args) override;

        // === Error Handling ===
        std::optional<::services::ScriptError> getLastError() const override;
        void clearError() override;

        // === Script Library Path ===
        void setScriptLibraryPath(const std::string& path) override;

    private:
        // Register all native engine APIs with mType
        void registerEngineAPIs();

        // Register individual API classes
        void registerEntityClass();
        void registerLogClass();
        void registerTimeClass();
        void registerAudioClass();
        void registerInputClass();

        // Helper: set error
        void setError(::services::ScriptError::Type type, const std::string& message,
                      const std::string& file = "", int line = 0);

        // Helper: extract class name from script file
        std::string extractClassName(const std::string& scriptPath);
    };

}
