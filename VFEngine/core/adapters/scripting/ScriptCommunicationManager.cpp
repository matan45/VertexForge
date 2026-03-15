// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>

#include "ScriptCommunicationManager.hpp"
#include "NativeAPIRegistry.hpp"
#include "../../services/events/EventDispatcher.hpp"
#include "../../services/events/project/SceneEvents.hpp"
#include "print/Log.hpp"

namespace core
{
    ScriptCommunicationManager::ScriptCommunicationManager(
        ::services::ScriptInterpreter* interpreter,
        const std::unordered_map<uint64_t, std::string>& instanceToClassName,
        const std::unordered_map<uint64_t, ::services::EntityHandle>& instanceToEntity,
        std::unordered_map<uint64_t, std::any>& instanceToObject)
        : interpreter(interpreter)
        , instanceToClassName(instanceToClassName)
        , instanceToEntity(instanceToEntity)
        , instanceToObject(instanceToObject)
    {
    }

    std::vector<uint64_t> ScriptCommunicationManager::findInstancesOnEntity(uint64_t entityId) const
    {
        std::vector<uint64_t> result;
        for (const auto& [instId, handle] : instanceToEntity)
        {
            if (handle.id == entityId)
            {
                result.push_back(instId);
            }
        }
        return result;
    }

    value::Value ScriptCommunicationManager::getScript(uint64_t entityId, const std::string& className)
    {
        for (const auto& [instId, handle] : instanceToEntity)
        {
            if (handle.id != entityId) continue;

            auto classIt = instanceToClassName.find(instId);
            if (classIt == instanceToClassName.end() || classIt->second != className) continue;

            auto objIt = instanceToObject.find(instId);
            if (objIt == instanceToObject.end()) continue;

            try
            {
                return std::any_cast<const value::Value&>(objIt->second);
            }
            catch (const std::bad_any_cast&)
            {
                vfLogError("[ScriptCommunication] Failed to cast script object for class '{}'", className);
            }
        }
        return value::Value(std::monostate{});
    }

    bool ScriptCommunicationManager::hasScript(uint64_t entityId, const std::string& className)
    {
        for (const auto& [instId, handle] : instanceToEntity)
        {
            if (handle.id != entityId) continue;

            auto classIt = instanceToClassName.find(instId);
            if (classIt != instanceToClassName.end() && classIt->second == className)
                return true;
        }
        return false;
    }

    void ScriptCommunicationManager::sendMessage(uint64_t entityId, const value::Value& callback,
                                                  const std::vector<value::Value>& args)
    {
        auto instances = findInstancesOnEntity(entityId);
        for (uint64_t instId : instances)
        {
            auto objIt = instanceToObject.find(instId);
            if (objIt == instanceToObject.end()) continue;

            try
            {
                auto entityIt = instanceToEntity.find(instId);
                if (entityIt != instanceToEntity.end())
                {
                    NativeAPIRegistry::setCurrentEntity(entityIt->second);
                    NativeAPIRegistry::setCurrentInstanceId(instId);
                }

                // Build args: script object + extra args
                auto& scriptObj = std::any_cast<const value::Value&>(objIt->second);
                std::vector<value::Value> callArgs;
                callArgs.push_back(scriptObj);
                callArgs.insert(callArgs.end(), args.begin(), args.end());

                interpreter->callLambda(callback, callArgs);
            }
            catch (const std::exception&)
            {
                // Silently skip on error
            }
        }
    }

    void ScriptCommunicationManager::broadcastMessage(uint64_t entityId, const value::Value& callback,
                                                       const std::vector<value::Value>& args)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        // Iterative BFS to avoid stack overflow on deep hierarchies
        std::vector<uint64_t> queue;
        queue.push_back(entityId);

        while (!queue.empty())
        {
            uint64_t current = queue.back();
            queue.pop_back();

            sendMessage(current, callback, args);

            events::scene::GetEntityQuery query;
            query.entity = services::EntityHandle{current};
            auto result = dispatcher.query(query);
            if (result.has_value())
            {
                for (const auto& child : result->children)
                {
                    queue.push_back(child.id);
                }
            }
        }
    }

    uint64_t ScriptCommunicationManager::listen(const std::string& eventName, uint64_t instanceId,
                                                 const value::Value& callback)
    {
        uint64_t token = nextToken++;
        eventListeners[eventName].push_back({token, instanceId, callback});
        return token;
    }

    void ScriptCommunicationManager::unlisten(uint64_t token)
    {
        for (auto& [eventName, listeners] : eventListeners)
        {
            for (auto it = listeners.begin(); it != listeners.end(); ++it)
            {
                if (it->token == token)
                {
                    listeners.erase(it);
                    return;
                }
            }
        }
    }

    void ScriptCommunicationManager::emit(const std::string& eventName,
                                           const std::vector<value::Value>& args)
    {
        auto it = eventListeners.find(eventName);
        if (it == eventListeners.end()) return;

        // Copy the listener list in case callbacks modify it
        auto listeners = it->second;
        for (const auto& listener : listeners)
        {
            try
            {
                auto entityIt = instanceToEntity.find(listener.ownerInstanceId);
                if (entityIt != instanceToEntity.end())
                {
                    NativeAPIRegistry::setCurrentEntity(entityIt->second);
                    NativeAPIRegistry::setCurrentInstanceId(listener.ownerInstanceId);
                }

                interpreter->callLambda(listener.callback, args);
            }
            catch (const std::exception& e)
            {
                vfLogWarning("[ScriptCommunication] Event '{}' listener callback failed: {}",
                             eventName, e.what());
            }
        }
    }

    void ScriptCommunicationManager::removeListenersForInstance(uint64_t instanceId)
    {
        for (auto& [eventName, listeners] : eventListeners)
        {
            listeners.erase(
                std::remove_if(listeners.begin(), listeners.end(),
                    [instanceId](const Listener& l) { return l.ownerInstanceId == instanceId; }),
                listeners.end());
        }
    }

    void ScriptCommunicationManager::clearAll()
    {
        eventListeners.clear();
        nextToken = 1;
    }
}
