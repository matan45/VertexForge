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
    class ScriptInputActionEventBridge
    {
    public:
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

        ::services::ScriptInterpreter* interpreter;
        const std::unordered_map<uint64_t, std::unordered_set<std::string>>& instanceToInterfaces;
        std::unordered_map<uint64_t, std::any>& instanceToObject;
        const std::unordered_map<uint64_t, ::services::EntityHandle>& instanceToEntity;
        const std::unordered_map<uint64_t, int>& instanceToPriority;

        std::vector<::events::SubscriptionToken> tokens;
    };
}
