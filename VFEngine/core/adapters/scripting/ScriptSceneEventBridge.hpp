#pragma once

#include <array>

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
        // Listener interfaces this bridge dispatches to. ScriptingAdapter
        // aggregates every bridge's kRequiredInterfaces into the set it probes
        // at loadScript time - dispatch gates must use these constants, never
        // fresh string literals, or the callback silently never fires.
        static constexpr const char* kSceneEventListener = "ISceneEventListener";
        static constexpr std::array<const char*, 1> kRequiredInterfaces = { kSceneEventListener };

        ScriptSceneEventBridge(
            ::services::ScriptInterpreter* interpreter,
            const std::unordered_map<uint64_t, std::unordered_set<std::string>>& instanceToInterfaces,
            std::unordered_map<uint64_t, std::any>& instanceToObject,
            const std::unordered_map<uint64_t, ::services::EntityHandle>& instanceToEntity);

        void subscribeAll();
        void unsubscribeAll();

        // Static callback storage for async scene loads.
        // Shared across all bridge instances â€” assumes single-interpreter model.
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
