// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>

#include "ScriptInputEventBridge.hpp"
#include "NativeAPIRegistry.hpp"
#include "events/input/InputEvents.hpp"

#include "print/Log.hpp"
namespace core
{
    ScriptInputEventBridge::ScriptInputEventBridge(
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

    void ScriptInputEventBridge::subscribeAll()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        tokens.push_back(dispatcher.subscribe<::events::input::KeyPressedNotification>(
            [this](const ::events::input::KeyPressedNotification& notif)
            {
                dispatchToAllListeners("onKeyPressed", {
                    value::Value(static_cast<int64_t>(notif.keyCode)),
                    value::Value(notif.shiftDown),
                    value::Value(notif.ctrlDown),
                    value::Value(notif.altDown),
                    value::Value(static_cast<double>(notif.mouseX)),
                    value::Value(static_cast<double>(notif.mouseY))
                });
            }));

        tokens.push_back(dispatcher.subscribe<::events::input::KeyReleasedNotification>(
            [this](const ::events::input::KeyReleasedNotification& notif)
            {
                dispatchToAllListeners("onKeyReleased", {
                    value::Value(static_cast<int64_t>(notif.keyCode)),
                    value::Value(notif.shiftDown),
                    value::Value(notif.ctrlDown),
                    value::Value(notif.altDown),
                    value::Value(static_cast<double>(notif.mouseX)),
                    value::Value(static_cast<double>(notif.mouseY))
                });
            }));

        tokens.push_back(dispatcher.subscribe<::events::input::MouseButtonPressedNotification>(
            [this](const ::events::input::MouseButtonPressedNotification& notif)
            {
                dispatchToAllListeners("onMouseButtonPressed", {
                    value::Value(static_cast<int64_t>(notif.button)),
                    value::Value(notif.shiftDown),
                    value::Value(notif.ctrlDown),
                    value::Value(notif.altDown),
                    value::Value(static_cast<double>(notif.mouseX)),
                    value::Value(static_cast<double>(notif.mouseY))
                });
            }));

        tokens.push_back(dispatcher.subscribe<::events::input::MouseButtonReleasedNotification>(
            [this](const ::events::input::MouseButtonReleasedNotification& notif)
            {
                dispatchToAllListeners("onMouseButtonReleased", {
                    value::Value(static_cast<int64_t>(notif.button)),
                    value::Value(notif.shiftDown),
                    value::Value(notif.ctrlDown),
                    value::Value(notif.altDown),
                    value::Value(static_cast<double>(notif.mouseX)),
                    value::Value(static_cast<double>(notif.mouseY))
                });
            }));

        vfLogInfo("[ScriptInputEventBridge] Subscribed to input events");
    }

    void ScriptInputEventBridge::unsubscribeAll()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();
        for (auto& token : tokens)
        {
            if (token.isValid()) dispatcher.unsubscribe(token);
        }
        tokens.clear();

        vfLogInfo("[ScriptInputEventBridge] Unsubscribed from input events");
    }

    void ScriptInputEventBridge::dispatchToAllListeners(
        const char* methodName, const std::vector<value::Value>& args)
    {
        const std::string requiredInterface = "IInputEventListener";

        for (const auto& [instanceId, interfaces] : instanceToInterfaces)
        {
            if (interfaces.find(requiredInterface) == interfaces.end())
                continue;

            auto objIt = instanceToObject.find(instanceId);
            if (objIt == instanceToObject.end()) continue;

            auto entityIt = instanceToEntity.find(instanceId);
            if (entityIt == instanceToEntity.end()) continue;

            try
            {
                NativeAPIRegistry::setCurrentEntity(entityIt->second);
                auto& instance = std::any_cast<value::Value&>(objIt->second);
                interpreter->callMethod(instance, methodName, args);
            }
            catch (const std::exception& e)
            {
                vfLogWarning("[ScriptInputEventBridge] {} callback error: {}", methodName, e.what());
            }
        }
    }
}
