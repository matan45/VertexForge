// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>

#include "ScriptInputActionEventBridge.hpp"
#include "NativeAPIRegistry.hpp"
#include "events/input/InputEvents.hpp"
#include "events/input/ActionMappingEvents.hpp"
#include "data/ActionMappingTypes.hpp"

#include "print/Log.hpp"
namespace core
{
    ScriptInputActionEventBridge::ScriptInputActionEventBridge(
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

    void ScriptInputActionEventBridge::subscribeAll()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        tokens.push_back(dispatcher.subscribe<::events::input::KeyPressedNotification>(
            [this](const ::events::input::KeyPressedNotification& notif)
            {
                onKeyOrButtonEvent(notif.keyCode, true, true, notif.shiftDown, notif.ctrlDown, notif.altDown);
            }));

        tokens.push_back(dispatcher.subscribe<::events::input::KeyReleasedNotification>(
            [this](const ::events::input::KeyReleasedNotification& notif)
            {
                onKeyOrButtonEvent(notif.keyCode, true, false, notif.shiftDown, notif.ctrlDown, notif.altDown);
            }));

        tokens.push_back(dispatcher.subscribe<::events::input::MouseButtonPressedNotification>(
            [this](const ::events::input::MouseButtonPressedNotification& notif)
            {
                onKeyOrButtonEvent(notif.button, false, true, notif.shiftDown, notif.ctrlDown, notif.altDown);
            }));

        tokens.push_back(dispatcher.subscribe<::events::input::MouseButtonReleasedNotification>(
            [this](const ::events::input::MouseButtonReleasedNotification& notif)
            {
                onKeyOrButtonEvent(notif.button, false, false, notif.shiftDown, notif.ctrlDown, notif.altDown);
            }));

        vfLogInfo("[ScriptInputActionEventBridge] Subscribed to input action events");
    }

    void ScriptInputActionEventBridge::unsubscribeAll()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();
        for (auto& token : tokens)
        {
            if (token.isValid()) dispatcher.unsubscribe(token);
        }
        tokens.clear();

        vfLogInfo("[ScriptInputActionEventBridge] Unsubscribed from input action events");
    }

    void ScriptInputActionEventBridge::onKeyOrButtonEvent(
        int code, bool isKey, bool isPressed, bool shiftDown, bool ctrlDown, bool altDown)
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        auto actionNames = dispatcher.query(::events::input::GetAllActionNamesQuery{});

        ::services::BindingType bindingType = isKey
            ? ::services::BindingType::Key
            : ::services::BindingType::MouseButton;

        const char* methodName = isPressed ? "onActionPressed" : "onActionReleased";

        for (const auto& actionName : actionNames)
        {
            ::events::input::GetActionBindingsQuery query;
            query.actionName = actionName;
            auto bindings = dispatcher.query(query);

            for (const auto& binding : bindings)
            {
                if (binding.type != bindingType || binding.code != code)
                    continue;

                // Check modifier requirements
                if (binding.requireShift && !shiftDown) continue;
                if (binding.requireCtrl && !ctrlDown) continue;
                if (binding.requireAlt && !altDown) continue;

                dispatchActionEvent(methodName, actionName);
                break;
            }
        }
    }

    void ScriptInputActionEventBridge::dispatchActionEvent(
        const char* methodName, const std::string& actionName)
    {
        const std::string requiredInterface = "IInputActionListener";

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
                interpreter->callMethod(instance, methodName, {value::Value(actionName)});
            }
            catch (const std::exception& e)
            {
                vfLogWarning("[ScriptInputActionEventBridge] {} callback error: {}", methodName, e.what());
            }
        }
    }
}
