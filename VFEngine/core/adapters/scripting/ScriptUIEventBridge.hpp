#pragma once

#include "../../services/events/EventDispatcher.hpp"
#include "../../services/providers/scripting/IScriptingProvider.hpp"
#include <value/ValueType.hpp>
#include <array>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <any>
#include <vector>
#include <functional>

namespace services
{
    class ScriptInterpreter;
}

namespace core
{
    class ScriptUIEventBridge
    {
    public:
        // Listener interfaces this bridge dispatches to. ScriptingAdapter
        // aggregates every bridge's kRequiredInterfaces into the set it probes
        // at loadScript time — dispatch gates must use these constants, never
        // fresh string literals, or the callback silently never fires.
        static constexpr const char* kButtonListener = "IUIButtonListener";
        static constexpr const char* kTextInputListener = "IUITextInputListener";
        static constexpr const char* kCheckboxListener = "IUICheckboxListener";
        static constexpr const char* kDropdownListener = "IUIDropdownListener";
        static constexpr const char* kTabsListener = "IUITabsListener";
        static constexpr const char* kSliderListener = "IUISliderListener";
        static constexpr const char* kProgressBarListener = "IUIProgressBarListener";
        static constexpr const char* kDragDropListener = "IUIDragDropListener";
        static constexpr const char* kWindowListener = "IUIWindowListener";
        static constexpr const char* kListViewListener = "IUIListViewListener";
        static constexpr std::array<const char*, 10> kRequiredInterfaces = {
            kButtonListener, kTextInputListener, kCheckboxListener,
            kDropdownListener, kTabsListener, kSliderListener,
            kProgressBarListener, kDragDropListener, kWindowListener,
            kListViewListener
        };

        ScriptUIEventBridge(
            ::services::ScriptInterpreter* interpreter,
            const std::unordered_map<uint64_t, std::unordered_set<std::string>>& instanceToInterfaces,
            std::unordered_map<uint64_t, std::any>& instanceToObject,
            const std::unordered_map<uint64_t, ::services::EntityHandle>& instanceToEntity);

        void subscribeAll();
        void unsubscribeAll();

    private:
        using ArgsBuilder = std::function<std::vector<value::Value>()>;

        void dispatchToListeners(const std::string& interfaceName,
                                 const char* methodName,
                                 ArgsBuilder buildArgs);

        // Widget-specific dispatch methods
        void dispatchButtonCallback(const char* methodName,
                                    ::services::EntityHandle entity,
                                    const std::string& entityName);

        void dispatchTextInputCallback(const char* methodName,
                                       ::services::EntityHandle entity,
                                       const std::string& entityName,
                                       const std::string& text = "");

        void dispatchCheckboxCallback(const char* methodName,
                                      ::services::EntityHandle entity,
                                      const std::string& entityName,
                                      bool newState = false, bool previousState = false);

        void dispatchDropdownCallback(const char* methodName,
                                      ::services::EntityHandle entity,
                                      const std::string& entityName,
                                      int previousIndex = -1, int newIndex = -1);

        void dispatchTabsCallback(const char* methodName,
                                  ::services::EntityHandle entity,
                                  const std::string& entityName,
                                  int tabIndex = -1, int previousTabIndex = -1);

        void dispatchSliderCallback(const char* methodName,
                                    ::services::EntityHandle entity,
                                    const std::string& entityName,
                                    float newValue = 0.0f, float previousValue = 0.0f,
                                    float finalValue = 0.0f);

        void dispatchProgressBarCallback(const char* methodName,
                                         ::services::EntityHandle entity,
                                         const std::string& entityName,
                                         float newValue = 0.0f, float previousValue = 0.0f);

        void dispatchWindowCallback(const char* methodName,
                                    ::services::EntityHandle entity,
                                    const std::string& entityName);

        void dispatchListViewCallback(const char* methodName,
                                      ::services::EntityHandle entity,
                                      const std::string& entityName,
                                      int previousIndex, int newIndex);

        void dispatchDragDropCallback(const char* methodName,
                                      ::services::EntityHandle sourceEntity,
                                      const std::string& sourceEntityName,
                                      ::services::EntityHandle targetEntity = {},
                                      const std::string& targetEntityName = "",
                                      const std::string& dragTag = "",
                                      bool wasDropped = false);

        ::services::ScriptInterpreter* interpreter;
        const std::unordered_map<uint64_t, std::unordered_set<std::string>>& instanceToInterfaces;
        std::unordered_map<uint64_t, std::any>& instanceToObject;
        const std::unordered_map<uint64_t, ::services::EntityHandle>& instanceToEntity;

        std::vector<::events::SubscriptionToken> tokens;
    };
}
