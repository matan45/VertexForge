#pragma once
#include "../../data/EntityHandle.hpp"
#include "../../data/ScriptTypes.hpp"
#include <string>
#include <optional>
#include <vector>
#include <functional>
#include <any>

namespace services {
    
    struct ScriptBuildResult {
        bool success = true;
        size_t filesCompiled = 0;
        size_t filesFailed = 0;
        std::vector<std::string> errors;
    };

    class IScriptingProvider {
    public:
        virtual ~IScriptingProvider() = default;

        // === VM Lifecycle ===
        virtual bool init() = 0;
        virtual void cleanUp() = 0;
        virtual bool isInitialized() const = 0;

        // === Script Building ===
        virtual ScriptBuildResult buildScripts(const std::string& manifestPath) = 0;
        
        virtual void cleanScripts(const std::string& manifestPath) = 0;
        
        virtual bool isCompiled() const = 0;
        
        virtual bool loadCompiledScripts(const std::string& manifestPath) = 0;

        // === Script Loading ===
        virtual std::optional<ScriptInstanceInfo> loadScript(
            const std::string& scriptPath,
            EntityHandle entity) = 0;
        virtual void unloadScript(uint64_t instanceId) = 0;
        virtual void unloadAllScripts() = 0;
        virtual bool isScriptLoaded(uint64_t instanceId) const = 0;

        // === Lifecycle Calls ===
        virtual void callOnStart(uint64_t instanceId) = 0;
        virtual void callOnUpdate(uint64_t instanceId, float deltaTime) = 0;
        virtual void callOnFixedUpdate(uint64_t instanceId, float fixedDeltaTime) = 0;
        virtual void callOnLateUpdate(uint64_t instanceId, float deltaTime) = 0;
        virtual void callOnEnable(uint64_t instanceId) = 0;
        virtual void callOnDisable(uint64_t instanceId) = 0;
        virtual void callOnDestroy(uint64_t instanceId) = 0;

        // === Coroutine Tick ===
        virtual void tickCoroutines(float deltaTime) = 0;
        virtual void tickFixedUpdateCoroutines() = 0;

        // === Generic Method Call ===
        virtual std::string callMethodWithReturn(uint64_t instanceId, const std::string& methodName,
                                                  const std::vector<std::any>& args = {}) = 0;

        // Whether the instance's class (or a base class) defines methodName — lets callers
        // skip optional callbacks without triggering a method-not-found error
        virtual bool hasMethod(uint64_t instanceId, const std::string& methodName) const = 0;

        virtual void playVFX(uint64_t instanceId) = 0;

        virtual void setInstancePriority(uint64_t instanceId, int priority) = 0;

        // === Error Handling ===
        virtual std::optional<ScriptError> getLastError() const = 0;
        virtual void clearError() = 0;

        // === Script Library Path ===
        virtual void setScriptLibraryPath(const std::string& path) = 0;

        // === Debugger (VK-1371) ===
        // Start/stop an embedded mType debug server that an external client (the
        // VS Code extension) attaches to over TCP to debug engine-run scripts.
        // isDebuggerActive() reports whether the server is currently listening.
        virtual void startDebugServer(int port) = 0;
        virtual void stopDebugServer() = 0;
        virtual bool isDebuggerActive() const = 0;

        // === Plugin Native Function Registration ===
        // The function is type-erased as std::any wrapping a services::NativeFunction
        // (mType's environment::registry::NativeDelegate — {void* userData, fn ptr}).
        virtual void registerPluginNativeFunction(const std::string& name, std::any function) = 0;
        // Remove a plugin-registered native (called on plugin unload so the
        // interpreter never holds a delegate into a freed DLL).
        virtual void unregisterPluginNativeFunction(const std::string& name) = 0;

        // === Save/Load State ===
        // Get JSON representation of a script instance's fields
        virtual std::string getInstanceState(uint64_t instanceId) = 0;
        // Restore script instance fields from JSON
        virtual bool setInstanceState(uint64_t instanceId, const std::string& jsonState) = 0;
        // Check if an instance's class has @Saveable annotation
        virtual bool isSaveableInstance(uint64_t instanceId) const = 0;
        // Get all live script instance IDs
        virtual std::vector<uint64_t> getAllInstanceIds() const = 0;
        // Get the entity handle for a script instance
        virtual EntityHandle getInstanceEntity(uint64_t instanceId) const = 0;
        // Get the class name for a script instance
        virtual std::string getInstanceClassName(uint64_t instanceId) const = 0;
        // Get the script path for a script instance
        virtual std::string getInstanceScriptPath(uint64_t instanceId) const = 0;
    };

}
