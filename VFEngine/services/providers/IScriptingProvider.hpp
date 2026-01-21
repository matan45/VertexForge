#pragma once
#include "../data/EntityHandle.hpp"
#include "../data/ScriptTypes.hpp"
#include <string>
#include <optional>
#include <vector>

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
        virtual void callOnDestroy(uint64_t instanceId) = 0;

        virtual void playVFX(uint64_t instanceId) = 0;

        // === Error Handling ===
        virtual std::optional<ScriptError> getLastError() const = 0;
        virtual void clearError() = 0;

        // === Script Library Path ===
        virtual void setScriptLibraryPath(const std::string& path) = 0;
    };

}
