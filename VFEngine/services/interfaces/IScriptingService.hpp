#pragma once
#include "../data/EntityHandle.hpp"
#include "../data/ScriptTypes.hpp"
#include <string>
#include <vector>
#include <any>
#include <optional>

namespace services {

    class IScriptingService {
    public:
        virtual ~IScriptingService() = default;

        // === Script Component (Multi-Script Support) ===

        // Attach a script to an entity (can attach multiple scripts)
        virtual bool attachScript(EntityHandle entity, const ScriptData& data) = 0;

        // Detach a specific script by path
        virtual void detachScript(EntityHandle entity, const std::string& scriptPath) = 0;

        // Detach all scripts from an entity
        virtual void detachAllScripts(EntityHandle entity) = 0;

        // Check if entity has any scripts
        virtual bool hasScripts(EntityHandle entity) const = 0;

        // Check if entity has a specific script
        virtual bool hasScript(EntityHandle entity, const std::string& scriptPath) const = 0;

        // Get all script paths attached to an entity
        virtual std::vector<std::string> getScriptPaths(EntityHandle entity) const = 0;

        // Enable/disable a specific script
        virtual void setScriptEnabled(EntityHandle entity, const std::string& scriptPath, bool enabled) = 0;

        // Check if a specific script is enabled
        virtual bool isScriptEnabled(EntityHandle entity, const std::string& scriptPath) const = 0;

        // === Script Properties (per script) ===

        virtual std::vector<ScriptPropertyInfo> getScriptProperties(EntityHandle entity,
                                                                     const std::string& scriptPath) const = 0;

        virtual bool setProperty(EntityHandle entity, const std::string& scriptPath,
                                 const std::string& propertyName, const std::any& value) = 0;

        virtual std::optional<std::any> getProperty(EntityHandle entity, const std::string& scriptPath,
                                                     const std::string& propertyName) const = 0;

        // === Method Calls (per script) ===

        virtual std::vector<ScriptMethodInfo> getScriptMethods(EntityHandle entity,
                                                                const std::string& scriptPath) const = 0;

        virtual std::optional<std::any> callMethod(EntityHandle entity, const std::string& scriptPath,
                                                    const std::string& methodName,
                                                    const std::vector<std::any>& args = {}) = 0;

        // === Events/Messages ===

        // Send message to all scripts on an entity
        virtual void sendMessage(EntityHandle entity, const std::string& messageName,
                                 const std::any& data = {}) = 0;

        // Broadcast message to all scripts on all entities
        virtual void broadcastMessage(const std::string& messageName,
                                       const std::any& data = {}) = 0;

        // === System Update ===

        virtual void updateScripts(float deltaTime) = 0;

        virtual void fixedUpdate(float fixedDeltaTime) = 0;

        virtual void lateUpdate(float deltaTime) = 0;

        // === Script Lifecycle Events ===

        // Trigger start for all scripts on entity
        virtual void triggerStart(EntityHandle entity) = 0;

        // Trigger destroy for all scripts on entity
        virtual void triggerDestroy(EntityHandle entity) = 0;

        // === Hot Reload ===

        virtual bool reloadScript(const std::string& scriptPath) = 0;

        virtual void reloadAllScripts() = 0;
    };

}
