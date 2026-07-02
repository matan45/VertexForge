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
    class ScriptAnimationEventBridge
    {
    private:
        ::services::ScriptInterpreter* interpreter;
        const std::unordered_map<uint64_t, std::unordered_set<std::string>>& instanceToInterfaces;
        std::unordered_map<uint64_t, std::any>& instanceToObject;
        const std::unordered_map<uint64_t, ::services::EntityHandle>& instanceToEntity;

        std::vector<::events::SubscriptionToken> tokens;
    public:
        // Listener interfaces this bridge dispatches to. ScriptingAdapter
        // aggregates every bridge's kRequiredInterfaces into the set it probes
        // at loadScript time - dispatch gates must use these constants, never
        // fresh string literals, or the callback silently never fires.
        static constexpr const char* kAnimationEventListener = "IAnimationEventListener";
        static constexpr std::array<const char*, 1> kRequiredInterfaces = { kAnimationEventListener };

        ScriptAnimationEventBridge(
            ::services::ScriptInterpreter* interpreter,
            const std::unordered_map<uint64_t, std::unordered_set<std::string>>& instanceToInterfaces,
            std::unordered_map<uint64_t, std::any>& instanceToObject,
            const std::unordered_map<uint64_t, ::services::EntityHandle>& instanceToEntity);

        void subscribeAll();
        void unsubscribeAll();

    private:
        void dispatchAnimationEvent(::services::EntityHandle entity,
                                    const std::string& eventName,
                                    const std::string& stateName,
                                    const std::string& payload);
    };
}
