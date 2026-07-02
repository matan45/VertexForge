#pragma once

#include <array>

#include "../../services/events/EventDispatcher.hpp"
#include "../../services/providers/scripting/IScriptingProvider.hpp"
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <any>
#include <vector>
#include <glm/glm.hpp>

namespace services
{
    class ScriptInterpreter;
}

namespace core
{
    class ScriptOceanEventBridge
    {
    public:
        // Listener interfaces this bridge dispatches to. ScriptingAdapter
        // aggregates every bridge's kRequiredInterfaces into the set it probes
        // at loadScript time - dispatch gates must use these constants, never
        // fresh string literals, or the callback silently never fires.
        static constexpr const char* kWaterListener = "IWaterListener";
        static constexpr std::array<const char*, 1> kRequiredInterfaces = { kWaterListener };

        ScriptOceanEventBridge(
            ::services::ScriptInterpreter* interpreter,
            const std::unordered_map<uint64_t, std::unordered_set<std::string>>& instanceToInterfaces,
            std::unordered_map<uint64_t, std::any>& instanceToObject,
            const std::unordered_map<uint64_t, ::services::EntityHandle>& instanceToEntity);

        void subscribeAll();
        void unsubscribeAll();

    private:
        void dispatchWaterEnter(::services::EntityHandle entity, const glm::vec3& position,
                                float verticalSpeed);
        void dispatchWaterExit(::services::EntityHandle entity);

        ::services::ScriptInterpreter* interpreter;
        const std::unordered_map<uint64_t, std::unordered_set<std::string>>& instanceToInterfaces;
        std::unordered_map<uint64_t, std::any>& instanceToObject;
        const std::unordered_map<uint64_t, ::services::EntityHandle>& instanceToEntity;

        std::vector<::events::SubscriptionToken> tokens;
    };
}
