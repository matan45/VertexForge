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

        // === Script Component ===
        
        virtual bool attachScript(EntityHandle entity, const ScriptData& data) = 0;
        
        virtual void detachScript(EntityHandle entity) = 0;
        
        virtual bool hasScript(EntityHandle entity) const = 0;
        
        virtual void setScriptEnabled(EntityHandle entity, bool enabled) = 0;
        
        virtual bool isScriptEnabled(EntityHandle entity) const = 0;

        // === Script Properties ===
        
        virtual std::vector<ScriptPropertyInfo> getScriptProperties(EntityHandle entity) const = 0;
        
        virtual bool setProperty(EntityHandle entity, const std::string& propertyName,
                                 const std::any& value) = 0;
        
        virtual std::optional<std::any> getProperty(EntityHandle entity,
                                                     const std::string& propertyName) const = 0;

        // === Method Calls ===
        
        virtual std::vector<ScriptMethodInfo> getScriptMethods(EntityHandle entity) const = 0;
        
        virtual std::optional<std::any> callMethod(EntityHandle entity,
                                                    const std::string& methodName,
                                                    const std::vector<std::any>& args = {}) = 0;

        // === Events/Messages ===
        
        virtual void sendMessage(EntityHandle entity, const std::string& messageName,
                                 const std::any& data = {}) = 0;
        
        virtual void broadcastMessage(const std::string& messageName,
                                       const std::any& data = {}) = 0;

        // === System Update ===
        
        virtual void updateScripts(float deltaTime) = 0;
        
        virtual void fixedUpdate(float fixedDeltaTime) = 0;
        
        virtual void lateUpdate(float deltaTime) = 0;

        // === Script Lifecycle Events ===
        
        virtual void triggerStart(EntityHandle entity) = 0;
        
        virtual void triggerDestroy(EntityHandle entity) = 0;

        // === Hot Reload ===
        
        virtual bool reloadScript(const std::string& scriptPath) = 0;
        
        virtual void reloadAllScripts() = 0;
    };

}
