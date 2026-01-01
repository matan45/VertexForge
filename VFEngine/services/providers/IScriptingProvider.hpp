#pragma once
#include "../data/EntityHandle.hpp"
#include "../data/ScriptTypes.hpp"
#include <string>
#include <vector>
#include <any>
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
        // Load a script and create an instance for an entity
        // Returns instance info on success, nullopt on failure
        virtual std::optional<ScriptInstanceInfo> loadScript(
            const std::string& scriptPath,
            EntityHandle entity) = 0;

        // Unload a script instance
        virtual void unloadScript(uint64_t instanceId) = 0;

        // Check if a script instance is loaded
        virtual bool isScriptLoaded(uint64_t instanceId) const = 0;

        // === Lifecycle Calls ===
        virtual void callOnStart(uint64_t instanceId) = 0;
        virtual void callOnUpdate(uint64_t instanceId, float deltaTime) = 0;
        virtual void callOnDestroy(uint64_t instanceId) = 0;

        // === Property Access ===
        // Get list of public properties exposed by the script
        virtual std::vector<ScriptPropertyInfo> getProperties(uint64_t instanceId) const = 0;

        // Set a property value
        virtual bool setProperty(uint64_t instanceId, const std::string& name,
                                  const std::any& value) = 0;

        // Get a property value
        virtual std::optional<std::any> getProperty(uint64_t instanceId,
                                                     const std::string& name) const = 0;

        // === Method Calls ===
        // Get list of public methods exposed by the script
        virtual std::vector<ScriptMethodInfo> getMethods(uint64_t instanceId) const = 0;

        // Call a method on the script instance
        virtual std::optional<std::any> callMethod(uint64_t instanceId,
                                                    const std::string& methodName,
                                                    const std::vector<std::any>& args) = 0;

        // === Error Handling ===
        // Get the last error that occurred (if any)
        virtual std::optional<ScriptError> getLastError() const = 0;

        // Clear the last error
        virtual void clearError() = 0;

        // === Script Library Path ===
        // Set the base path for script imports
        virtual void setScriptLibraryPath(const std::string& path) = 0;
    };

}
