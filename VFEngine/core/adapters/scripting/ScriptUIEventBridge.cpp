// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>

#include "ScriptUIEventBridge.hpp"
#include "NativeAPIRegistry.hpp"
#include "events/ui/UIEvents.hpp"

#include "print/Log.hpp"
namespace core
{
    ScriptUIEventBridge::ScriptUIEventBridge(
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

    void ScriptUIEventBridge::dispatchToListeners(
        const std::string& interfaceName,
        const char* methodName,
        ArgsBuilder buildArgs)
    {
        for (const auto& [instanceId, interfaces] : instanceToInterfaces)
        {
            if (interfaces.find(interfaceName) == interfaces.end())
            {
                continue;
            }

            auto objIt = instanceToObject.find(instanceId);
            if (objIt == instanceToObject.end())
            {
                continue;
            }

            auto entityIt = instanceToEntity.find(instanceId);
            auto entity = entityIt != instanceToEntity.end()
                ? entityIt->second
                : ::services::EntityHandle::invalid();

            try
            {
                AmbientScriptContext ctx(entity, instanceId);
                auto& instance = std::any_cast<value::Value&>(objIt->second);
                interpreter->callMethod(instance, methodName, buildArgs());
            }
            catch (const std::exception& e)
            {
                vfLogWarning("[ScriptingAdapter] {} callback error: {}", methodName, e.what());
            }
        }
    }

    // ============================================
    // Button
    // ============================================

    void ScriptUIEventBridge::dispatchButtonCallback(
        const char* methodName,
        ::services::EntityHandle entity,
        const std::string& entityName)
    {
        dispatchToListeners(kButtonListener, methodName, [&]()
        {
            return std::vector<value::Value>{
                value::Value(static_cast<int>(entity.id)),
                value::Value(entityName)
            };
        });
    }

    // ============================================
    // TextInput
    // ============================================

    void ScriptUIEventBridge::dispatchTextInputCallback(
        const char* methodName,
        ::services::EntityHandle entity,
        const std::string& entityName,
        const std::string& text)
    {
        dispatchToListeners(kTextInputListener, methodName, [&]()
        {
            std::string_view method(methodName);
            if (text.empty() &&
                (method == "onTextInputFocused" || method == "onTextInputUnfocused"))
            {
                return std::vector<value::Value>{
                    value::Value(static_cast<int>(entity.id)),
                    value::Value(entityName)
                };
            }
            return std::vector<value::Value>{
                value::Value(static_cast<int>(entity.id)),
                value::Value(entityName),
                value::Value(text)
            };
        });
    }

    // ============================================
    // Checkbox
    // ============================================

    void ScriptUIEventBridge::dispatchCheckboxCallback(
        const char* methodName,
        ::services::EntityHandle entity,
        const std::string& entityName,
        bool newState, bool previousState)
    {
        dispatchToListeners(kCheckboxListener, methodName, [&]()
        {
            std::string_view method(methodName);
            if (method == "onCheckboxToggled")
            {
                return std::vector<value::Value>{
                    value::Value(static_cast<int>(entity.id)),
                    value::Value(entityName),
                    value::Value(newState),
                    value::Value(previousState)
                };
            }
            return std::vector<value::Value>{
                value::Value(static_cast<int>(entity.id)),
                value::Value(entityName)
            };
        });
    }

    // ============================================
    // Dropdown
    // ============================================

    void ScriptUIEventBridge::dispatchDropdownCallback(
        const char* methodName,
        ::services::EntityHandle entity,
        const std::string& entityName,
        int previousIndex, int newIndex)
    {
        dispatchToListeners(kDropdownListener, methodName, [&]()
        {
            std::string_view method(methodName);
            if (method == "onDropdownSelectionChanged")
            {
                return std::vector<value::Value>{
                    value::Value(static_cast<int>(entity.id)),
                    value::Value(entityName),
                    value::Value(static_cast<int64_t>(previousIndex)),
                    value::Value(static_cast<int64_t>(newIndex))
                };
            }
            return std::vector<value::Value>{
                value::Value(static_cast<int>(entity.id)),
                value::Value(entityName)
            };
        });
    }

    // ============================================
    // Tabs
    // ============================================

    void ScriptUIEventBridge::dispatchTabsCallback(
        const char* methodName,
        ::services::EntityHandle entity,
        const std::string& entityName,
        int tabIndex, int previousTabIndex)
    {
        dispatchToListeners(kTabsListener, methodName, [&]()
        {
            std::string_view method(methodName);
            if (method == "onTabChanged")
            {
                return std::vector<value::Value>{
                    value::Value(static_cast<int>(entity.id)),
                    value::Value(entityName),
                    value::Value(static_cast<int64_t>(tabIndex)),
                    value::Value(static_cast<int64_t>(previousTabIndex))
                };
            }
            return std::vector<value::Value>{
                value::Value(static_cast<int>(entity.id)),
                value::Value(entityName),
                value::Value(static_cast<int64_t>(tabIndex))
            };
        });
    }

    // ============================================
    // Slider
    // ============================================

    void ScriptUIEventBridge::dispatchSliderCallback(
        const char* methodName,
        ::services::EntityHandle entity,
        const std::string& entityName,
        float newValue, float previousValue, float finalValue)
    {
        dispatchToListeners(kSliderListener, methodName, [&]()
        {
            std::string_view method(methodName);
            if (method == "onSliderValueChanged")
            {
                return std::vector<value::Value>{
                    value::Value(static_cast<int>(entity.id)),
                    value::Value(entityName),
                    value::Value(static_cast<float>(newValue)),
                    value::Value(static_cast<float>(previousValue))
                };
            }
            if (method == "onSliderDragEnd")
            {
                return std::vector<value::Value>{
                    value::Value(static_cast<int>(entity.id)),
                    value::Value(entityName),
                    value::Value(static_cast<float>(finalValue))
                };
            }
            return std::vector<value::Value>{
                value::Value(static_cast<int>(entity.id)),
                value::Value(entityName)
            };
        });
    }

    // ============================================
    // ProgressBar
    // ============================================

    void ScriptUIEventBridge::dispatchProgressBarCallback(
        const char* methodName,
        ::services::EntityHandle entity,
        const std::string& entityName,
        float newValue, float previousValue)
    {
        dispatchToListeners(kProgressBarListener, methodName, [&]()
        {
            std::string_view method(methodName);
            if (method == "onProgressBarValueChanged")
            {
                return std::vector<value::Value>{
                    value::Value(static_cast<int>(entity.id)),
                    value::Value(entityName),
                    value::Value(static_cast<float>(newValue)),
                    value::Value(static_cast<float>(previousValue))
                };
            }
            return std::vector<value::Value>{
                value::Value(static_cast<int>(entity.id)),
                value::Value(entityName)
            };
        });
    }

    void ScriptUIEventBridge::dispatchDragDropCallback(
        const char* methodName,
        ::services::EntityHandle sourceEntity,
        const std::string& sourceEntityName,
        ::services::EntityHandle targetEntity,
        const std::string& targetEntityName,
        const std::string& dragTag,
        bool wasDropped)
    {
        // Safe to capture methodName (const char*) by reference: dispatchToListeners is synchronous
        dispatchToListeners(kDragDropListener, methodName, [&]()
        {
            std::string_view method(methodName);
            if (method == "onDragStart")
            {
                return std::vector<value::Value>{
                    value::Value(static_cast<int>(sourceEntity.id)),
                    value::Value(sourceEntityName),
                    value::Value(dragTag)
                };
            }
            if (method == "onDragEnd")
            {
                return std::vector<value::Value>{
                    value::Value(static_cast<int>(sourceEntity.id)),
                    value::Value(sourceEntityName),
                    value::Value(wasDropped)
                };
            }
            if (method == "onDrop")
            {
                return std::vector<value::Value>{
                    value::Value(static_cast<int>(sourceEntity.id)),
                    value::Value(sourceEntityName),
                    value::Value(static_cast<int>(targetEntity.id)),
                    value::Value(targetEntityName),
                    value::Value(dragTag)
                };
            }
            return std::vector<value::Value>{
                value::Value(static_cast<int>(sourceEntity.id)),
                value::Value(sourceEntityName)
            };
        });
    }

    // ============================================
    // Window
    // ============================================

    void ScriptUIEventBridge::dispatchWindowCallback(
        const char* methodName,
        ::services::EntityHandle entity,
        const std::string& entityName)
    {
        dispatchToListeners(kWindowListener, methodName, [&]()
        {
            return std::vector<value::Value>{
                value::Value(static_cast<int>(entity.id)),
                value::Value(entityName)
            };
        });
    }

    // ============================================
    // ListView
    // ============================================

    void ScriptUIEventBridge::dispatchListViewCallback(
        const char* methodName,
        ::services::EntityHandle entity,
        const std::string& entityName,
        int previousIndex, int newIndex)
    {
        dispatchToListeners(kListViewListener, methodName, [&]()
        {
            return std::vector<value::Value>{
                value::Value(static_cast<int>(entity.id)),
                value::Value(entityName),
                value::Value(static_cast<int64_t>(previousIndex)),
                value::Value(static_cast<int64_t>(newIndex))
            };
        });
    }

    // ============================================
    // Subscribe / Unsubscribe All
    // ============================================

    void ScriptUIEventBridge::subscribeAll()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        // Button events
        tokens.push_back(dispatcher.subscribe<::events::ui::UIButtonClickedNotification>(
            [this](const auto& n) { dispatchButtonCallback("onButtonClicked", n.entity, n.entityName); }));
        tokens.push_back(dispatcher.subscribe<::events::ui::UIButtonPressedNotification>(
            [this](const auto& n) { dispatchButtonCallback("onButtonPressed", n.entity, n.entityName); }));
        tokens.push_back(dispatcher.subscribe<::events::ui::UIButtonReleasedNotification>(
            [this](const auto& n) { dispatchButtonCallback("onButtonReleased", n.entity, n.entityName); }));
        tokens.push_back(dispatcher.subscribe<::events::ui::UIButtonHoverEnterNotification>(
            [this](const auto& n) { dispatchButtonCallback("onButtonHoverEnter", n.entity, n.entityName); }));
        tokens.push_back(dispatcher.subscribe<::events::ui::UIButtonHoverExitNotification>(
            [this](const auto& n) { dispatchButtonCallback("onButtonHoverExit", n.entity, n.entityName); }));

        // TextInput events
        tokens.push_back(dispatcher.subscribe<::events::ui::UITextInputSubmitNotification>(
            [this](const auto& n) { dispatchTextInputCallback("onTextInputSubmit", n.entity, n.entityName, n.text); }));
        tokens.push_back(dispatcher.subscribe<::events::ui::UITextInputChangedNotification>(
            [this](const auto& n) { dispatchTextInputCallback("onTextInputChanged", n.entity, n.entityName, n.text); }));
        tokens.push_back(dispatcher.subscribe<::events::ui::UITextInputFocusedNotification>(
            [this](const auto& n) { dispatchTextInputCallback("onTextInputFocused", n.entity, n.entityName); }));
        tokens.push_back(dispatcher.subscribe<::events::ui::UITextInputUnfocusedNotification>(
            [this](const auto& n) { dispatchTextInputCallback("onTextInputUnfocused", n.entity, n.entityName); }));

        // Checkbox events
        tokens.push_back(dispatcher.subscribe<::events::ui::UICheckboxToggledNotification>(
            [this](const auto& n) { dispatchCheckboxCallback("onCheckboxToggled", n.entity, n.entityName, n.newCheckedState, n.previousCheckedState); }));
        tokens.push_back(dispatcher.subscribe<::events::ui::UICheckboxHoverEnterNotification>(
            [this](const auto& n) { dispatchCheckboxCallback("onCheckboxHoverEnter", n.entity, n.entityName); }));
        tokens.push_back(dispatcher.subscribe<::events::ui::UICheckboxHoverExitNotification>(
            [this](const auto& n) { dispatchCheckboxCallback("onCheckboxHoverExit", n.entity, n.entityName); }));

        // Dropdown events
        tokens.push_back(dispatcher.subscribe<::events::ui::UIDropdownOpenedNotification>(
            [this](const auto& n) { dispatchDropdownCallback("onDropdownOpened", n.entity, n.entityName); }));
        tokens.push_back(dispatcher.subscribe<::events::ui::UIDropdownClosedNotification>(
            [this](const auto& n) { dispatchDropdownCallback("onDropdownClosed", n.entity, n.entityName); }));
        tokens.push_back(dispatcher.subscribe<::events::ui::UIDropdownSelectionChangedNotification>(
            [this](const auto& n) { dispatchDropdownCallback("onDropdownSelectionChanged", n.entity, n.entityName, n.previousIndex, n.newIndex); }));

        // Tabs events
        tokens.push_back(dispatcher.subscribe<::events::ui::UITabSelectedNotification>(
            [this](const auto& n) { dispatchTabsCallback("onTabSelected", n.entity, n.entityName, n.tabIndex); }));
        tokens.push_back(dispatcher.subscribe<::events::ui::UITabChangedNotification>(
            [this](const auto& n) { dispatchTabsCallback("onTabChanged", n.entity, n.entityName, n.newTabIndex, n.previousTabIndex); }));

        // Slider events
        tokens.push_back(dispatcher.subscribe<::events::ui::UISliderValueChangedNotification>(
            [this](const auto& n) { dispatchSliderCallback("onSliderValueChanged", n.entity, n.entityName, n.newValue, n.previousValue); }));
        tokens.push_back(dispatcher.subscribe<::events::ui::UISliderDragStartNotification>(
            [this](const auto& n) { dispatchSliderCallback("onSliderDragStart", n.entity, n.entityName); }));
        tokens.push_back(dispatcher.subscribe<::events::ui::UISliderDragEndNotification>(
            [this](const auto& n) { dispatchSliderCallback("onSliderDragEnd", n.entity, n.entityName, 0.0f, 0.0f, n.finalValue); }));
        tokens.push_back(dispatcher.subscribe<::events::ui::UISliderHoverEnterNotification>(
            [this](const auto& n) { dispatchSliderCallback("onSliderHoverEnter", n.entity, n.entityName); }));
        tokens.push_back(dispatcher.subscribe<::events::ui::UISliderHoverExitNotification>(
            [this](const auto& n) { dispatchSliderCallback("onSliderHoverExit", n.entity, n.entityName); }));

        // ProgressBar events
        tokens.push_back(dispatcher.subscribe<::events::ui::UIProgressBarValueChangedNotification>(
            [this](const auto& n) { dispatchProgressBarCallback("onProgressBarValueChanged", n.entity, n.entityName, n.newValue, n.previousValue); }));
        tokens.push_back(dispatcher.subscribe<::events::ui::UIProgressBarCompletedNotification>(
            [this](const auto& n) { dispatchProgressBarCallback("onProgressBarCompleted", n.entity, n.entityName); }));

        // ListView events
        tokens.push_back(dispatcher.subscribe<::events::ui::UIListSelectionChangedNotification>(
            [this](const auto& n) { dispatchListViewCallback("onListSelectionChanged", n.entity, n.entityName, n.previousIndex, n.newIndex); }));

        // Window events
        tokens.push_back(dispatcher.subscribe<::events::ui::UIWindowOpenedNotification>(
            [this](const auto& n) { dispatchWindowCallback("onWindowOpened", n.entity, n.entityName); }));
        tokens.push_back(dispatcher.subscribe<::events::ui::UIWindowClosedNotification>(
            [this](const auto& n) { dispatchWindowCallback("onWindowClosed", n.entity, n.entityName); }));

        // Drag & Drop events
        tokens.push_back(dispatcher.subscribe<::events::ui::UIDragStartNotification>(
            [this](const auto& n) { dispatchDragDropCallback("onDragStart", n.entity, n.entityName, {}, "", n.dragTag); }));
        tokens.push_back(dispatcher.subscribe<::events::ui::UIDragEndNotification>(
            [this](const auto& n) { dispatchDragDropCallback("onDragEnd", n.entity, n.entityName, {}, "", "", n.wasDropped); }));
        tokens.push_back(dispatcher.subscribe<::events::ui::UIDropNotification>(
            [this](const auto& n) { dispatchDragDropCallback("onDrop", n.sourceEntity, n.sourceEntityName, n.targetEntity, n.targetEntityName, n.dragTag); }));

        vfLogInfo("[ScriptingAdapter] Subscribed to all UI events ({} subscriptions)", tokens.size());
    }

    void ScriptUIEventBridge::unsubscribeAll()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        for (auto& token : tokens)
        {
            if (token.isValid())
            {
                dispatcher.unsubscribe(token);
            }
        }
        tokens.clear();

        vfLogInfo("[ScriptingAdapter] Unsubscribed from all UI events");
    }
}
