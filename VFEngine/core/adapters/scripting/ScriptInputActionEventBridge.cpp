// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>
#include <value/ValueShim.hpp>

#include "ScriptInputActionEventBridge.hpp"
#include "NativeAPIRegistry.hpp"
#include "events/input/InputEvents.hpp"
#include "events/input/ActionMappingEvents.hpp"
#include "data/ActionMappingTypes.hpp"
#include <algorithm>

#include "print/Log.hpp"
namespace core
{
    ScriptInputActionEventBridge::ScriptInputActionEventBridge(
        ::services::ScriptInterpreter* interpreter,
        const std::unordered_map<uint64_t, std::unordered_set<std::string>>& instanceToInterfaces,
        std::unordered_map<uint64_t, std::any>& instanceToObject,
        const std::unordered_map<uint64_t, ::services::EntityHandle>& instanceToEntity,
        const std::unordered_map<uint64_t, int>& instanceToPriority)
        : interpreter(interpreter)
        , instanceToInterfaces(instanceToInterfaces)
        , instanceToObject(instanceToObject)
        , instanceToEntity(instanceToEntity)
        , instanceToPriority(instanceToPriority)
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

        tokens.push_back(dispatcher.subscribe<::events::input::ActionMappingChangedNotification>(
            [this](const ::events::input::ActionMappingChangedNotification&)
            {
                bindingCacheDirty = true;
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

    void ScriptInputActionEventBridge::rebuildBindingCache()
    {
        bindingCache.clear();
        auto& dispatcher = ::events::EventDispatcher::instance();

        auto actionNames = dispatcher.query(::events::input::GetAllActionNamesQuery{});
        for (const auto& actionName : actionNames)
        {
            ::events::input::GetActionBindingsQuery query;
            query.actionName = actionName;
            auto bindings = dispatcher.query(query);

            for (const auto& binding : bindings)
            {
                int cacheKey = (static_cast<int>(binding.type) << 16) | binding.code;
                bindingCache[cacheKey].push_back({actionName, binding.requireShift, binding.requireCtrl, binding.requireAlt});
            }
        }
        bindingCacheDirty = false;
    }

    void ScriptInputActionEventBridge::onKeyOrButtonEvent(
        int code, bool isKey, bool isPressed, bool shiftDown, bool ctrlDown, bool altDown)
    {
        if (bindingCacheDirty) rebuildBindingCache();

        int bindingType = isKey ? static_cast<int>(::services::BindingType::Key)
                                : static_cast<int>(::services::BindingType::MouseButton);
        int key = (bindingType << 16) | code;

        auto it = bindingCache.find(key);
        if (it == bindingCache.end()) return;

        const char* methodName = isPressed ? "onActionPressed" : "onActionReleased";

        for (const auto& cached : it->second)
        {
            if (cached.requireShift && !shiftDown) continue;
            if (cached.requireCtrl && !ctrlDown) continue;
            if (cached.requireAlt && !altDown) continue;

            dispatchActionEvent(methodName, cached.actionName);
        }
    }

    void ScriptInputActionEventBridge::dispatchActionEvent(
        const char* methodName, const std::string& actionName)
    {
        // Check if already consumed
        auto& dispatcher = ::events::EventDispatcher::instance();
        ::events::input::IsActionConsumedQuery consumedQuery;
        consumedQuery.actionName = actionName;
        if (dispatcher.query(consumedQuery)) return;

        const std::string requiredInterface = "IInputActionListener";

        // Collect eligible instances with priority
        struct DispatchEntry
        {
            uint64_t instanceId;
            int priority;
        };
        std::vector<DispatchEntry> entries;

        for (const auto& [instanceId, interfaces] : instanceToInterfaces)
        {
            if (interfaces.find(requiredInterface) == interfaces.end())
                continue;
            if (instanceToObject.find(instanceId) == instanceToObject.end())
                continue;
            if (instanceToEntity.find(instanceId) == instanceToEntity.end())
                continue;

            int priority = 0;
            auto prioIt = instanceToPriority.find(instanceId);
            if (prioIt != instanceToPriority.end()) priority = prioIt->second;
            entries.push_back({instanceId, priority});
        }

        // Sort by priority descending (higher priority dispatched first)
        std::sort(entries.begin(), entries.end(),
            [](const DispatchEntry& a, const DispatchEntry& b)
            {
                return a.priority > b.priority;
            });

        for (const auto& [instanceId, priority] : entries)
        {
            auto objIt = instanceToObject.find(instanceId);
            auto entityIt = instanceToEntity.find(instanceId);

            try
            {
                NativeAPIRegistry::setCurrentEntity(entityIt->second);
                NativeAPIRegistry::setCurrentInstanceId(instanceId);
                auto& instance = std::any_cast<value::Value&>(objIt->second);
                auto result = interpreter->callMethod(instance, methodName, {value::Value(actionName)});

                // If callback returns true, consume the action and stop propagation
                if (value::isBool(result) && value::asBool(result))
                {
                    ::events::input::ConsumeActionCommand cmd;
                    cmd.actionName = actionName;
                    dispatcher.execute(cmd);
                    return;
                }
            }
            catch (const std::exception& e)
            {
                vfLogWarning("[ScriptInputActionEventBridge] {} callback error: {}", methodName, e.what());
            }
        }
    }
}
