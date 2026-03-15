// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>

#include "ScriptPhysicsEventBridge.hpp"
#include "NativeAPIRegistry.hpp"
#include "events/physics/PhysicsEvents.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"

#include "print/Log.hpp"
namespace core
{
    ScriptPhysicsEventBridge::ScriptPhysicsEventBridge(
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

    void ScriptPhysicsEventBridge::subscribeAll()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        tokens.push_back(dispatcher.subscribe<::events::physics::CollisionStartNotification>(
            [this](const ::events::physics::CollisionStartNotification& notif)
            {
                dispatchCollisionCallback("onCollisionEnter", notif.entityA, notif.entityB);
                dispatchCollisionCallback("onCollisionEnter", notif.entityB, notif.entityA);
            }));

        tokens.push_back(dispatcher.subscribe<::events::physics::CollisionEndNotification>(
            [this](const ::events::physics::CollisionEndNotification& notif)
            {
                dispatchCollisionCallback("onCollisionExit", notif.entityA, notif.entityB);
                dispatchCollisionCallback("onCollisionExit", notif.entityB, notif.entityA);
            }));

        tokens.push_back(dispatcher.subscribe<::events::physics::TriggerEnterNotification>(
            [this](const ::events::physics::TriggerEnterNotification& notif)
            {
                dispatchCollisionCallback("onTriggerEnter", notif.triggerEntity, notif.otherEntity);
            }));

        tokens.push_back(dispatcher.subscribe<::events::physics::TriggerExitNotification>(
            [this](const ::events::physics::TriggerExitNotification& notif)
            {
                dispatchCollisionCallback("onTriggerExit", notif.triggerEntity, notif.otherEntity);
            }));

        vfLogInfo("[ScriptPhysicsEventBridge] Subscribed to physics collision events");
    }

    void ScriptPhysicsEventBridge::unsubscribeAll()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();
        for (auto& token : tokens)
        {
            if (token.isValid()) dispatcher.unsubscribe(token);
        }
        tokens.clear();

        vfLogInfo("[ScriptPhysicsEventBridge] Unsubscribed from physics collision events");
    }

    void ScriptPhysicsEventBridge::dispatchCollisionCallback(
        const char* methodName, ::services::EntityHandle self, ::services::EntityHandle other)
    {
        auto& registry = scene::EntityRegistry::getRegistry();

        if (!registry.valid(static_cast<entt::entity>(self.id))) return;

        auto* scriptComp = registry.try_get<components::ScriptComponent>(
            static_cast<entt::entity>(self.id));
        if (!scriptComp) return;

        std::string requiredInterface;
        std::string methodStr(methodName);
        if (methodStr == "onCollisionEnter" || methodStr == "onCollisionExit")
            requiredInterface = "ICollisionListener";
        else if (methodStr == "onTriggerEnter" || methodStr == "onTriggerExit")
            requiredInterface = "ITriggerListener";
        else
            return;

        for (const auto& [instanceId, entityHandle] : instanceToEntity)
        {
            if (entityHandle.id != self.id) continue;

            auto interfaceIt = instanceToInterfaces.find(instanceId);
            if (interfaceIt == instanceToInterfaces.end() ||
                interfaceIt->second.find(requiredInterface) == interfaceIt->second.end())
                continue;

            auto objIt = instanceToObject.find(instanceId);
            if (objIt == instanceToObject.end()) continue;

            try
            {
                NativeAPIRegistry::setCurrentEntity(self);
                auto& instance = std::any_cast<value::Value&>(objIt->second);
                interpreter->callMethod(instance, methodName,
                                        {value::Value(static_cast<int>(other.id))});
            }
            catch (const std::exception& e)
            {
                vfLogWarning("[ScriptPhysicsEventBridge] {} callback error: {}", methodName, e.what());
            }
        }
    }
}
