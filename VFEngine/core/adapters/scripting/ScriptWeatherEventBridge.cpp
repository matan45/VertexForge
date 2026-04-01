// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>
#include <value/NativeArray.hpp>

#include "ScriptWeatherEventBridge.hpp"
#include "NativeAPIRegistry.hpp"
#include "../api/NativeHelpers.hpp"
#include "../../services/events/EventDispatcher.hpp"
#include "events/weather/WeatherEvents.hpp"

#include "print/Log.hpp"

namespace core
{
    ScriptWeatherEventBridge::ScriptWeatherEventBridge(
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

    void ScriptWeatherEventBridge::subscribeAll()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        tokens.push_back(dispatcher.subscribe<::events::weather::WeatherStateChangedNotification>(
            [this](const ::events::weather::WeatherStateChangedNotification& notif)
            {
                auto prevArr = api::makeWeatherStateArray(notif.previousState);
                auto newArr = api::makeWeatherStateArray(notif.newState);
                dispatchGlobalCallback("onWeatherChanged", {prevArr, newArr});
            }));

        tokens.push_back(dispatcher.subscribe<::events::weather::LightningStrikeNotification>(
            [this](const ::events::weather::LightningStrikeNotification& notif)
            {
                auto posArr = api::makeVec3Array(notif.position);
                dispatchGlobalCallback("onLightningStrike", {posArr, value::Value(notif.intensity)});
            }));

        tokens.push_back(dispatcher.subscribe<::events::weather::WeatherZoneEnteredNotification>(
            [this](const ::events::weather::WeatherZoneEnteredNotification& notif)
            {
                dispatchEntityCallback("onWeatherZoneEntered", notif.entityId);
            }));

        tokens.push_back(dispatcher.subscribe<::events::weather::WeatherZoneExitedNotification>(
            [this](const ::events::weather::WeatherZoneExitedNotification& notif)
            {
                dispatchEntityCallback("onWeatherZoneExited", notif.entityId);
            }));

        vfLogInfo("[ScriptWeatherEventBridge] Subscribed to weather events");
    }

    void ScriptWeatherEventBridge::unsubscribeAll()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();
        for (auto& token : tokens)
        {
            if (token.isValid()) dispatcher.unsubscribe(token);
        }
        tokens.clear();

        vfLogInfo("[ScriptWeatherEventBridge] Unsubscribed from weather events");
    }

    void ScriptWeatherEventBridge::dispatchGlobalCallback(
        const char* methodName, const std::vector<value::Value>& args)
    {
        const std::string requiredInterface = "IWeatherEventListener";

        for (const auto& [instanceId, entityHandle] : instanceToEntity)
        {
            auto interfaceIt = instanceToInterfaces.find(instanceId);
            if (interfaceIt == instanceToInterfaces.end() ||
                interfaceIt->second.find(requiredInterface) == interfaceIt->second.end())
                continue;

            auto objIt = instanceToObject.find(instanceId);
            if (objIt == instanceToObject.end()) continue;

            try
            {
                NativeAPIRegistry::setCurrentEntity(entityHandle);
                auto& instance = std::any_cast<value::Value&>(objIt->second);
                interpreter->callMethod(instance, methodName, args);
            }
            catch (const std::exception& e)
            {
                vfLogWarning("[ScriptWeatherEventBridge] {} callback error: {}", methodName, e.what());
            }
        }
    }

    void ScriptWeatherEventBridge::dispatchEntityCallback(
        const char* methodName, uint32_t entityId)
    {
        const std::string requiredInterface = "IWeatherEventListener";

        for (const auto& [instanceId, entityHandle] : instanceToEntity)
        {
            if (static_cast<uint32_t>(entityHandle.id) != entityId) continue;

            auto interfaceIt = instanceToInterfaces.find(instanceId);
            if (interfaceIt == instanceToInterfaces.end() ||
                interfaceIt->second.find(requiredInterface) == interfaceIt->second.end())
                continue;

            auto objIt = instanceToObject.find(instanceId);
            if (objIt == instanceToObject.end()) continue;

            try
            {
                NativeAPIRegistry::setCurrentEntity(entityHandle);
                auto& instance = std::any_cast<value::Value&>(objIt->second);
                interpreter->callMethod(instance, methodName, {});
            }
            catch (const std::exception& e)
            {
                vfLogWarning("[ScriptWeatherEventBridge] {} callback error: {}", methodName, e.what());
            }
        }
    }
}
