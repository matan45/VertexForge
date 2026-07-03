#pragma once

#include <array>

#include "../../services/events/EventTypes.hpp"
#include "../../services/data/EntityHandle.hpp"
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <any>
#include <vector>
#include <cstdint>

namespace services
{
    class ScriptInterpreter;
}

namespace core
{
    class ScriptDestructionEventBridge
    {
    public:
        // Listener interfaces this bridge dispatches to. ScriptingAdapter
        // aggregates every bridge's kRequiredInterfaces into the set it probes
        // at loadScript time - dispatch gates must use these constants, never
        // fresh string literals, or the callback silently never fires.
        static constexpr const char* kDestructionListener = "IDestructionListener";
        static constexpr std::array<const char*, 1> kRequiredInterfaces = { kDestructionListener };

        ScriptDestructionEventBridge(
            ::services::ScriptInterpreter* interpreter,
            const std::unordered_map<uint64_t, std::unordered_set<std::string>>& instanceToInterfaces,
            std::unordered_map<uint64_t, std::any>& instanceToObject,
            const std::unordered_map<uint64_t, ::services::EntityHandle>& instanceToEntity);

        void subscribeAll();
        void unsubscribeAll();

    private:
        ::services::ScriptInterpreter* interpreter;
        const std::unordered_map<uint64_t, std::unordered_set<std::string>>& instanceToInterfaces;
        std::unordered_map<uint64_t, std::any>& instanceToObject;
        const std::unordered_map<uint64_t, ::services::EntityHandle>& instanceToEntity;

        std::vector<::events::SubscriptionToken> tokens;
    };
}
