#pragma once

#include <value/ValueType.hpp>
#include "../../services/data/EntityHandle.hpp"

#include <string>
#include <vector>
#include <unordered_map>
#include <any>
#include <cstdint>

namespace services
{
    class ScriptInterpreter;
}

namespace core
{
    class ScriptCommunicationManager
    {
    private:
        ::services::ScriptInterpreter* interpreter;
        const std::unordered_map<uint64_t, std::string>& instanceToClassName;
        const std::unordered_map<uint64_t, ::services::EntityHandle>& instanceToEntity;
        const std::unordered_map<uint64_t, std::any>& instanceToObject;

        struct Listener
        {
            uint64_t token;
            uint64_t ownerInstanceId;
            value::Value callback;
        };

        std::unordered_map<std::string, std::vector<Listener>> eventListeners;
        uint64_t nextToken = 1;

    public:
        ScriptCommunicationManager(
            ::services::ScriptInterpreter* interpreter,
            const std::unordered_map<uint64_t, std::string>& instanceToClassName,
            const std::unordered_map<uint64_t, ::services::EntityHandle>& instanceToEntity,
            const std::unordered_map<uint64_t, std::any>& instanceToObject);

        ~ScriptCommunicationManager() = default;

        // Get a script object by entity ID and class name
        value::Value getScript(uint64_t entityId, const std::string& className);

        // Check if an entity has a loaded script of a given class name
        bool hasScript(uint64_t entityId, const std::string& className);

        // Call a lambda for each script attached to an entity, passing the script object as argument
        void sendMessage(uint64_t entityId, const value::Value& callback,
                         const std::vector<value::Value>& args);

        // Call a lambda for each script on an entity and all descendants recursively
        void broadcastMessage(uint64_t entityId, const value::Value& callback,
                              const std::vector<value::Value>& args);

        // Subscribe: when eventName fires, invoke the callback lambda
        uint64_t listen(const std::string& eventName, uint64_t instanceId,
                        const value::Value& callback);

        // Unsubscribe by token
        void unlisten(uint64_t token);

        // Fire event to all listeners
        void emit(const std::string& eventName, const std::vector<value::Value>& args);

        // Remove all listeners owned by a specific script instance
        void removeListenersForInstance(uint64_t instanceId);

        // Clear all event listeners
        void clearAll();

    private:
        // Find all instanceIds belonging to an entity
        std::vector<uint64_t> findInstancesOnEntity(uint64_t entityId) const;
    };
}
