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
    class ScriptNavigationEventBridge
    {
    public:
        // Listener interfaces this bridge dispatches to. ScriptingAdapter
        // aggregates every bridge's kRequiredInterfaces into the set it probes
        // at loadScript time - dispatch gates must use these constants, never
        // fresh string literals, or the callback silently never fires.
        static constexpr const char* kNavigationEventListener = "INavigationEventListener";
        static constexpr std::array<const char*, 1> kRequiredInterfaces = { kNavigationEventListener };

        ScriptNavigationEventBridge(
            ::services::ScriptInterpreter* interpreter,
            const std::unordered_map<uint64_t, std::unordered_set<std::string>>& instanceToInterfaces,
            std::unordered_map<uint64_t, std::any>& instanceToObject,
            const std::unordered_map<uint64_t, ::services::EntityHandle>& instanceToEntity);

        void subscribeAll();
        void unsubscribeAll();

    private:
        void dispatchNavigationCallback(const char* methodName,
                                         ::services::EntityHandle entity);

        ::services::ScriptInterpreter* interpreter;
        const std::unordered_map<uint64_t, std::unordered_set<std::string>>& instanceToInterfaces;
        std::unordered_map<uint64_t, std::any>& instanceToObject;
        const std::unordered_map<uint64_t, ::services::EntityHandle>& instanceToEntity;

        std::vector<::events::SubscriptionToken> tokens;
    };
}
