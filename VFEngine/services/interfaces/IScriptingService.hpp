#pragma once
#include "../data/EntityHandle.hpp"
#include <string>
#include <vector>
#include <any>
#include <optional>

namespace services {

    /**
     * @brief Data for script component.
     */
    struct ScriptData {
        std::string scriptPath;     // Path to the script file
        bool enabled = true;
        // Initial property values can be set via setProperty after attachment
    };

    /**
     * @brief Information about a script's public property.
     */
    struct ScriptPropertyInfo {
        std::string name;
        std::string typeName;       // "float", "int", "string", "vec3", etc.
        bool isReadOnly = false;
    };

    /**
     * @brief Information about a script's callable method.
     */
    struct ScriptMethodInfo {
        std::string name;
        std::vector<std::string> parameterTypes;
        std::string returnType;
    };

    /**
     * @brief Service interface for script/behavior operations.
     *
     * This service provides high-level APIs for attaching and managing
     * scripts on entities using mtype scripting system.
     *
     * NOTE: This is an interface stub. Implementations will be added
     * when the scripting system is fully integrated.
     */
    class IScriptingService {
    public:
        virtual ~IScriptingService() = default;

        // === Script Component ===

        /**
         * @brief Attach a script to an entity.
         * @param entity Target entity
         * @param data Script configuration
         * @return true if script attached successfully
         */
        virtual bool attachScript(EntityHandle entity, const ScriptData& data) = 0;

        /**
         * @brief Detach script from an entity.
         */
        virtual void detachScript(EntityHandle entity) = 0;

        /**
         * @brief Check if entity has a script attached.
         */
        virtual bool hasScript(EntityHandle entity) const = 0;

        /**
         * @brief Enable/disable script execution on an entity.
         */
        virtual void setScriptEnabled(EntityHandle entity, bool enabled) = 0;

        /**
         * @brief Check if entity's script is enabled.
         */
        virtual bool isScriptEnabled(EntityHandle entity) const = 0;

        // === Script Properties ===

        /**
         * @brief Get list of public properties exposed by the script.
         */
        virtual std::vector<ScriptPropertyInfo> getScriptProperties(EntityHandle entity) const = 0;

        /**
         * @brief Set a script property value.
         * @param entity Target entity
         * @param propertyName Name of the property
         * @param value Value to set (type must match property type)
         * @return true if property was set successfully
         */
        virtual bool setProperty(EntityHandle entity, const std::string& propertyName,
                                 const std::any& value) = 0;

        /**
         * @brief Get a script property value.
         * @return Property value, or empty optional if not found
         */
        virtual std::optional<std::any> getProperty(EntityHandle entity,
                                                     const std::string& propertyName) const = 0;

        // === Method Calls ===

        /**
         * @brief Get list of callable methods exposed by the script.
         */
        virtual std::vector<ScriptMethodInfo> getScriptMethods(EntityHandle entity) const = 0;

        /**
         * @brief Call a method on the script.
         * @param entity Target entity
         * @param methodName Name of the method
         * @param args Arguments to pass (must match method signature)
         * @return Return value, or empty optional for void methods
         */
        virtual std::optional<std::any> callMethod(EntityHandle entity,
                                                    const std::string& methodName,
                                                    const std::vector<std::any>& args = {}) = 0;

        // === Events/Messages ===

        /**
         * @brief Send a message to a script.
         * @param entity Target entity
         * @param messageName Name of the message/event
         * @param data Optional data to pass with the message
         */
        virtual void sendMessage(EntityHandle entity, const std::string& messageName,
                                 const std::any& data = {}) = 0;

        /**
         * @brief Broadcast a message to all scripts.
         */
        virtual void broadcastMessage(const std::string& messageName,
                                       const std::any& data = {}) = 0;

        // === System Update ===

        /**
         * @brief Update all scripts (called once per frame).
         * This calls the Update() method on all enabled scripts.
         */
        virtual void updateScripts(float deltaTime) = 0;

        /**
         * @brief Called at the start of each frame before physics.
         */
        virtual void fixedUpdate(float fixedDeltaTime) = 0;

        /**
         * @brief Called at the end of each frame after rendering.
         */
        virtual void lateUpdate(float deltaTime) = 0;

        // === Script Lifecycle Events ===

        /**
         * @brief Manually trigger Start() on an entity's script.
         * Normally called automatically when script is first enabled.
         */
        virtual void triggerStart(EntityHandle entity) = 0;

        /**
         * @brief Manually trigger OnDestroy() on an entity's script.
         * Normally called automatically when entity/script is removed.
         */
        virtual void triggerDestroy(EntityHandle entity) = 0;

        // === Hot Reload ===

        /**
         * @brief Reload a script file (for development).
         * @param scriptPath Path to the script to reload
         * @return true if reload was successful
         */
        virtual bool reloadScript(const std::string& scriptPath) = 0;

        /**
         * @brief Reload all scripts.
         */
        virtual void reloadAllScripts() = 0;
    };

}
