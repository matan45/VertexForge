#pragma once

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
