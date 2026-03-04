// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>

#include "ScriptAnimationEventBridge.hpp"
#include "NativeAPIRegistry.hpp"
#include "../../services/events/AnimationEventEvents.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"

namespace core
{
    ScriptAnimationEventBridge::ScriptAnimationEventBridge(
        ::services::ScriptInterpreter* interpreter,
        const std::unordered_map<uint64_t, std::unordered_set<std::string>>& instanceToInterfaces,
        const std::unordered_map<uint64_t, std::any>& instanceToObject,
        const std::unordered_map<uint64_t, ::services::EntityHandle>& instanceToEntity)
        : interpreter(interpreter)
        , instanceToInterfaces(instanceToInterfaces)
        , instanceToObject(instanceToObject)
        , instanceToEntity(instanceToEntity)
    {
    }

    void ScriptAnimationEventBridge::subscribeAll()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        tokens.push_back(dispatcher.subscribe<::events::animation::AnimationEventFiredNotification>(
            [this](const ::events::animation::AnimationEventFiredNotification& notif)
            {
                dispatchAnimationEvent(notif.entity, notif.eventName, notif.stateName, notif.payload);
            }));

        vfLogInfo("[ScriptAnimationEventBridge] Subscribed to animation events");
    }

    void ScriptAnimationEventBridge::unsubscribeAll()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();
        for (auto& token : tokens)
        {
            if (token.isValid()) dispatcher.unsubscribe(token);
        }
        tokens.clear();

        vfLogInfo("[ScriptAnimationEventBridge] Unsubscribed from animation events");
    }

    void ScriptAnimationEventBridge::dispatchAnimationEvent(
        ::services::EntityHandle entity,
        const std::string& eventName,
        const std::string& stateName,
        const std::string& payload)
    {
        auto& registry = scene::EntityRegistry::getRegistry();

        if (!registry.valid(static_cast<entt::entity>(entity.id))) return;

        auto* scriptComp = registry.try_get<components::ScriptComponent>(
            static_cast<entt::entity>(entity.id));
        if (!scriptComp) return;

        for (const auto& [instanceId, entityHandle] : instanceToEntity)
        {
            if (entityHandle.id != entity.id) continue;

            auto interfaceIt = instanceToInterfaces.find(instanceId);
            if (interfaceIt == instanceToInterfaces.end() ||
                interfaceIt->second.find("IAnimationEventListener") == interfaceIt->second.end())
                continue;

            auto objIt = instanceToObject.find(instanceId);
            if (objIt == instanceToObject.end()) continue;

            try
            {
                NativeAPIRegistry::setCurrentEntity(entity);
                auto& instance = std::any_cast<value::Value&>(const_cast<std::any&>(objIt->second));
                interpreter->callMethod(instance, "onAnimationEvent",
                                        {value::Value(eventName),
                                         value::Value(stateName),
                                         value::Value(payload)});
            }
            catch (const std::exception& e)
            {
                vfLogWarning("[ScriptAnimationEventBridge] onAnimationEvent callback error: {}", e.what());
            }
        }
    }
}
