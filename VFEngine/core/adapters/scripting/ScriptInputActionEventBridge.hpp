#pragma once

#include <array>

#include "../../services/events/EventDispatcher.hpp"
#include "../../services/providers/scripting/IScriptingProvider.hpp"
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <any>
#include <vector>

namespace services
{
    class ScriptInterpreter;
}

namespace core
{
    class ScriptInputActionEventBridge
    {
    public:
        // Listener interfaces this bridge dispatches to. ScriptingAdapter
        // aggregates every bridge's kRequiredInterfaces into the set it probes
        // at loadScript time - dispatch gates must use these constants, never
        // fresh string literals, or the callback silently never fires.
        static constexpr const char* kInputActionListener = "IInputActionListener";
        static constexpr std::array<const char*, 1> kRequiredInterfaces = { kInputActionListener };

        ScriptInputActionEventBridge(
            ::services::ScriptInterpreter* interpreter,
            const std::unordered_map<uint64_t, std::unordered_set<std::string>>& instanceToInterfaces,
            std::unordered_map<uint64_t, std::any>& instanceToObject,
            const std::unordered_map<uint64_t, ::services::EntityHandle>& instanceToEntity,
            const std::unordered_map<uint64_t, int>& instanceToPriority);

        void subscribeAll();
        void unsubscribeAll();

    private:
        void onKeyOrButtonEvent(int code, bool isKey, bool isPressed,
                                bool shiftDown, bool ctrlDown, bool altDown);
        void dispatchActionEvent(const char* methodName, const std::string& actionName);
        void rebuildBindingCache();

        ::services::ScriptInterpreter* interpreter;
        const std::unordered_map<uint64_t, std::unordered_set<std::string>>& instanceToInterfaces;
        std::unordered_map<uint64_t, std::any>& instanceToObject;
        const std::unordered_map<uint64_t, ::services::EntityHandle>& instanceToEntity;
        const std::unordered_map<uint64_t, int>& instanceToPriority;

        // Reverse lookup: (bindingType << 16 | code) -> list of (actionName, modifiers)
        struct CachedBinding {
            std::string actionName;
            bool requireShift = false;
            bool requireCtrl = false;
            bool requireAlt = false;
        };
        std::unordered_map<int, std::vector<CachedBinding>> bindingCache;
        bool bindingCacheDirty = true;

        std::vector<::events::SubscriptionToken> tokens;
    };
}
