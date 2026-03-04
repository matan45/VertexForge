#pragma once

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
    class ScriptPhysicsEventBridge
    {
    public:
        ScriptPhysicsEventBridge(
            ::services::ScriptInterpreter* interpreter,
            const std::unordered_map<uint64_t, std::unordered_set<std::string>>& instanceToInterfaces,
            const std::unordered_map<uint64_t, std::any>& instanceToObject,
            const std::unordered_map<uint64_t, ::services::EntityHandle>& instanceToEntity);

        void subscribeAll();
        void unsubscribeAll();

    private:
        void dispatchCollisionCallback(const char* methodName,
                                       ::services::EntityHandle self,
                                       ::services::EntityHandle other);

        ::services::ScriptInterpreter* interpreter;
        const std::unordered_map<uint64_t, std::unordered_set<std::string>>& instanceToInterfaces;
        const std::unordered_map<uint64_t, std::any>& instanceToObject;
        const std::unordered_map<uint64_t, ::services::EntityHandle>& instanceToEntity;

        std::vector<::events::SubscriptionToken> tokens;
    };
}
