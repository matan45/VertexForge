// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>

#include "CoroutineAPI.hpp"
#include "NativeHelpers.hpp"
#include "../scripting/CoroutineManager.hpp"
#include "../scripting/NativeAPIRegistry.hpp"

#include "print/Log.hpp"
namespace core::api
{
    void CoroutineAPI::setCoroutineManager(CoroutineManager* manager)
    {
        coroutineManager = manager;
    }

    void CoroutineAPI::registerAPI(services::ScriptInterpreter* interpreter)
    {
        interpreter->registerNativeFunction("_native_coroutine_waitForSeconds",
            [](const std::vector<value::Value>& args) -> value::Value
            {
                if (!coroutineManager || args.empty())
                    return value::Value(std::monostate{});

                double seconds = static_cast<double>(extractFloat(args[0]));
                uint64_t instanceId = NativeAPIRegistry::getCurrentInstanceId();
                return coroutineManager->waitForSeconds(instanceId, seconds);
            });

        interpreter->registerNativeFunction("_native_coroutine_waitForFrames",
            [](const std::vector<value::Value>& args) -> value::Value
            {
                if (!coroutineManager || args.empty())
                    return value::Value(std::monostate{});

                int frames = static_cast<int>(extractInt64(args[0]));
                uint64_t instanceId = NativeAPIRegistry::getCurrentInstanceId();
                return coroutineManager->waitForFrames(instanceId, frames);
            });

        interpreter->registerNativeFunction("_native_coroutine_waitForNextFrame",
            [](const std::vector<value::Value>& args) -> value::Value
            {
                if (!coroutineManager)
                    return value::Value(std::monostate{});

                uint64_t instanceId = NativeAPIRegistry::getCurrentInstanceId();
                return coroutineManager->waitForFrames(instanceId, 1);
            });

        interpreter->registerNativeFunction("_native_coroutine_waitForFixedUpdate",
            [](const std::vector<value::Value>& args) -> value::Value
            {
                if (!coroutineManager)
                    return value::Value(std::monostate{});

                uint64_t instanceId = NativeAPIRegistry::getCurrentInstanceId();
                return coroutineManager->waitForFixedUpdate(instanceId);
            });

        vfLogInfo("[CoroutineAPI] Registered Coroutine native functions");
    }
}
