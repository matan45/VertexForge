// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>

#include "ScriptNavigationEventBridge.hpp"
#include "NativeAPIRegistry.hpp"
#include "events/navmesh/NavmeshEvents.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"

#include "print/Log.hpp"
namespace core
{
    ScriptNavigationEventBridge::ScriptNavigationEventBridge(
        ::services::ScriptInterpreter* interpreter,
        const std::unordered_map<uint64_t, std::unordered_set<std::string>>& instanceToInterfaces,
        std::unordered_map<uint64_t, std::any>& instanceToObject,
        const std::unordered_map<uint64_t, ::services::EntityHandle>& instanceToEntity)
        : interpreter(interpreter)
        , instanceToInterfaces(instanceToInterfaces)
        , instanceToObject(instanceToObject)
        , instanceToEntity(instanceToEntity)
    {
    }

    void ScriptNavigationEventBridge::subscribeAll()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        tokens.push_back(dispatcher.subscribe<::events::navmesh::AgentReachedDestinationNotification>(
            [this](const ::events::navmesh::AgentReachedDestinationNotification& notif)
            {
                dispatchNavigationCallback("onDestinationReached", notif.entity);
            }));

        tokens.push_back(dispatcher.subscribe<::events::navmesh::AgentPathBlockedNotification>(
            [this](const ::events::navmesh::AgentPathBlockedNotification& notif)
            {
                dispatchNavigationCallback("onPathBlocked", notif.entity);
            }));

        vfLogInfo("[ScriptNavigationEventBridge] Subscribed to navigation events");
    }

    void ScriptNavigationEventBridge::unsubscribeAll()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();
        for (auto& token : tokens)
        {
            if (token.isValid()) dispatcher.unsubscribe(token);
        }
        tokens.clear();

        vfLogInfo("[ScriptNavigationEventBridge] Unsubscribed from navigation events");
    }

    void ScriptNavigationEventBridge::dispatchNavigationCallback(
        const char* methodName, ::services::EntityHandle entity)
    {
        auto& registry = scene::EntityRegistry::getRegistry();

        if (!registry.valid(static_cast<entt::entity>(entity.id))) return;

        auto* scriptComp = registry.try_get<components::ScriptComponent>(
            static_cast<entt::entity>(entity.id));
        if (!scriptComp) return;

        const std::string requiredInterface = kNavigationEventListener;

        for (const auto& [instanceId, entityHandle] : instanceToEntity)
        {
            if (entityHandle.id != entity.id) continue;

            auto interfaceIt = instanceToInterfaces.find(instanceId);
            if (interfaceIt == instanceToInterfaces.end() ||
                interfaceIt->second.find(requiredInterface) == interfaceIt->second.end())
                continue;

            auto objIt = instanceToObject.find(instanceId);
            if (objIt == instanceToObject.end()) continue;

            try
            {
                AmbientScriptContext ctx(entity, instanceId);
                auto& instance = std::any_cast<value::Value&>(objIt->second);
                interpreter->callMethod(instance, methodName, {});
            }
            catch (const std::exception& e)
            {
                vfLogWarning("[ScriptNavigationEventBridge] {} callback error: {}", methodName, e.what());
            }
        }
    }
}
