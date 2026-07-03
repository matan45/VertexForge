#pragma once

#include "../../services/events/EventDispatcher.hpp"
#include "../../services/providers/scripting/IScriptingProvider.hpp"
#include <array>
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
    class ScriptPhysicsEventBridge
    {
    public:
        // Listener interfaces this bridge dispatches to. ScriptingAdapter
        // aggregates every bridge's kRequiredInterfaces into the set it probes
        // at loadScript time — dispatch gates must use these constants, never
        // fresh string literals, or the callback silently never fires.
        static constexpr const char* kCollisionListener = "ICollisionListener";
        static constexpr const char* kTriggerListener = "ITriggerListener";
        static constexpr const char* kRagdollListener = "IRagdollListener";
        static constexpr std::array<const char*, 3> kRequiredInterfaces = {
            kCollisionListener, kTriggerListener, kRagdollListener
        };

        ScriptPhysicsEventBridge(
            ::services::ScriptInterpreter* interpreter,
            const std::unordered_map<uint64_t, std::unordered_set<std::string>>& instanceToInterfaces,
            std::unordered_map<uint64_t, std::any>& instanceToObject,
            const std::unordered_map<uint64_t, ::services::EntityHandle>& instanceToEntity);

        void subscribeAll();
        void unsubscribeAll();

    private:
        void dispatchCollisionCallback(const char* methodName,
                                       ::services::EntityHandle self,
                                       ::services::EntityHandle other);
        void dispatchRagdollCallback(const char* methodName, ::services::EntityHandle self);

        ::services::ScriptInterpreter* interpreter;
        const std::unordered_map<uint64_t, std::unordered_set<std::string>>& instanceToInterfaces;
        std::unordered_map<uint64_t, std::any>& instanceToObject;
        const std::unordered_map<uint64_t, ::services::EntityHandle>& instanceToEntity;

        std::vector<::events::SubscriptionToken> tokens;
    };
}
