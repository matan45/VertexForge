// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>

#include "ScriptOceanEventBridge.hpp"
#include "NativeAPIRegistry.hpp"
#include "events/terrain/OceanEvents.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"

#include "print/Log.hpp"

namespace core
{
    ScriptOceanEventBridge::ScriptOceanEventBridge(
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

    void ScriptOceanEventBridge::subscribeAll()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        tokens.push_back(dispatcher.subscribe<::events::ocean::ObjectEnteredWaterNotification>(
            [this](const ::events::ocean::ObjectEnteredWaterNotification& notif)
            {
                dispatchWaterEnter(notif.entity, notif.position, notif.verticalSpeed);
            }));

        tokens.push_back(dispatcher.subscribe<::events::ocean::ObjectExitedWaterNotification>(
            [this](const ::events::ocean::ObjectExitedWaterNotification& notif)
            {
                dispatchWaterExit(notif.entity);
            }));

        vfLogInfo("[ScriptOceanEventBridge] Subscribed to water enter/exit events");
    }

    void ScriptOceanEventBridge::unsubscribeAll()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();
        for (auto& token : tokens)
        {
            if (token.isValid()) dispatcher.unsubscribe(token);
        }
        tokens.clear();

        vfLogInfo("[ScriptOceanEventBridge] Unsubscribed from water enter/exit events");
    }

    void ScriptOceanEventBridge::dispatchWaterEnter(::services::EntityHandle entity,
                                                    const glm::vec3& position,
                                                    float verticalSpeed)
    {
        for (const auto& [instanceId, interfaces] : instanceToInterfaces)
        {
            if (interfaces.find(kWaterListener) == interfaces.end()) continue;

            auto entityIt = instanceToEntity.find(instanceId);
            bool isSelf = entityIt != instanceToEntity.end() && entityIt->second.id == entity.id;

            auto objIt = instanceToObject.find(instanceId);
            if (objIt == instanceToObject.end()) continue;

            try
            {
                AmbientScriptContext ctx(
                    entityIt != instanceToEntity.end() ? entityIt->second
                                                       : ::services::EntityHandle::invalid(),
                    instanceId);

                auto& instance = std::any_cast<value::Value&>(objIt->second);
                if (isSelf)
                {
                    interpreter->callMethod(instance, "onWaterEnter",
                                            {value::Value(verticalSpeed)});
                }
                interpreter->callMethod(instance, "onEntityWaterEnter",
                                        {value::Value(static_cast<int>(entity.id)),
                                         value::Value(position.x), value::Value(position.y),
                                         value::Value(position.z), value::Value(verticalSpeed)});
            }
            catch (const std::exception& e)
            {
                vfLogWarning("[ScriptOceanEventBridge] onWaterEnter callback error: {}", e.what());
            }
        }
    }

    void ScriptOceanEventBridge::dispatchWaterExit(::services::EntityHandle entity)
    {
        for (const auto& [instanceId, interfaces] : instanceToInterfaces)
        {
            if (interfaces.find(kWaterListener) == interfaces.end()) continue;

            auto entityIt = instanceToEntity.find(instanceId);
            bool isSelf = entityIt != instanceToEntity.end() && entityIt->second.id == entity.id;

            auto objIt = instanceToObject.find(instanceId);
            if (objIt == instanceToObject.end()) continue;

            try
            {
                AmbientScriptContext ctx(
                    entityIt != instanceToEntity.end() ? entityIt->second
                                                       : ::services::EntityHandle::invalid(),
                    instanceId);

                auto& instance = std::any_cast<value::Value&>(objIt->second);
                if (isSelf)
                {
                    interpreter->callMethod(instance, "onWaterExit", {});
                }
                interpreter->callMethod(instance, "onEntityWaterExit",
                                        {value::Value(static_cast<int>(entity.id))});
            }
            catch (const std::exception& e)
            {
                vfLogWarning("[ScriptOceanEventBridge] onWaterExit callback error: {}", e.what());
            }
        }
    }
}
