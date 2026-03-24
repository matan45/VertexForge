#pragma once

#include "../../services/events/EventDispatcher.hpp"
#include "../../services/providers/scripting/IScriptingProvider.hpp"
#include <value/ValueType.hpp>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <any>
#include <vector>
#include <mutex>

namespace services
{
    class ScriptInterpreter;
}

namespace core
{
    class ScriptSceneEventBridge
    {
    public:
        ScriptSceneEventBridge(
            ::services::ScriptInterpreter* interpreter,
            const std::unordered_map<uint64_t, std::unordered_set<std::string>>& instanceToInterfaces,
            std::unordered_map<uint64_t, std::any>& instanceToObject,
            const std::unordered_map<uint64_t, ::services::EntityHandle>& instanceToEntity);

        void subscribeAll();
        void unsubscribeAll();

        // Static callback storage for async scene loads
        static void storeAsyncCallback(const std::string& scenePath, const value::Value& callback);

    private:
        void dispatchSceneCallback(const char* methodName, const std::string& sceneName);
        void resolveAsyncCallback(const std::string& scenePath, bool success);

        ::services::ScriptInterpreter* interpreter;
        const std::unordered_map<uint64_t, std::unordered_set<std::string>>& instanceToInterfaces;
        std::unordered_map<uint64_t, std::any>& instanceToObject;
        const std::unordered_map<uint64_t, ::services::EntityHandle>& instanceToEntity;

        std::vector<::events::SubscriptionToken> tokens;

        static inline std::unordered_map<std::string, value::Value> asyncCallbacks;
        static inline std::mutex asyncCallbackMutex;
    };
}
