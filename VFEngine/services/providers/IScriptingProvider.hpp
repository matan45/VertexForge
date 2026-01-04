#pragma once
#include "../data/EntityHandle.hpp"
#include "../data/ScriptTypes.hpp"
#include <string>
#include <optional>

namespace services {

    class IScriptingProvider {
    public:
        virtual ~IScriptingProvider() = default;

        // === VM Lifecycle ===
        virtual bool init() = 0;
        virtual void cleanUp() = 0;
        virtual bool isInitialized() const = 0;

        // === Script Loading ===
        virtual std::optional<ScriptInstanceInfo> loadScript(
            const std::string& scriptPath,
            EntityHandle entity) = 0;
        virtual void unloadScript(uint64_t instanceId) = 0;
        virtual bool isScriptLoaded(uint64_t instanceId) const = 0;

        // === Lifecycle Calls ===
        virtual void callOnStart(uint64_t instanceId) = 0;
        virtual void callOnUpdate(uint64_t instanceId, float deltaTime) = 0;
        virtual void callOnDestroy(uint64_t instanceId) = 0;

        // === Error Handling ===
        virtual std::optional<ScriptError> getLastError() const = 0;
        virtual void clearError() = 0;

        // === Script Library Path ===
        virtual void setScriptLibraryPath(const std::string& path) = 0;
    };

}
