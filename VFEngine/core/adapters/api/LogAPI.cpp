// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>

#include "LogAPI.hpp"
#include "NativeHelpers.hpp"

namespace core::api
{
    void LogAPI::registerAPI(services::ScriptInterpreter* interpreter)
    {
        interpreter->registerNativeFunction("_native_log_info",
            [](const std::vector<value::Value>& args) -> value::Value
            {
                if (!args.empty())
                {
                    std::string message = extractString(args[0]);
                    if (!message.empty())
                    {
                        vfLogInfo("[Script] {}", message);
                    }
                }
                return value::Value(std::monostate{});
            });

        interpreter->registerNativeFunction("_native_log_warn",
            [](const std::vector<value::Value>& args) -> value::Value
            {
                if (!args.empty())
                {
                    std::string message = extractString(args[0]);
                    if (!message.empty())
                    {
                        vfLogWarning("[Script] {}", message);
                    }
                }
                return value::Value(std::monostate{});
            });

        interpreter->registerNativeFunction("_native_log_error",
            [](const std::vector<value::Value>& args) -> value::Value
            {
                if (!args.empty())
                {
                    std::string message = extractString(args[0]);
                    if (!message.empty())
                    {
                        vfLogError("[Script] {}", message);
                    }
                }
                return value::Value(std::monostate{});
            });
    }
}
