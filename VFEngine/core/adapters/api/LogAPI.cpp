// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>
#include <environment/NativeContext.hpp>
#include <span>

#include "LogAPI.hpp"
#include "NativeHelpers.hpp"

namespace core::api
{
    void LogAPI::registerAPI(services::ScriptInterpreter* interpreter)
    {
        interpreter->registerNativeFunction("_native_log_info",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (!args.empty())
                {
                    std::string message = extractString(args[0]);
                    if (!message.empty())
                    {
                        vfLogInfo("[Script] {}", message);
                    }
                }
                return value::Value(std::monostate{});
            }});

        interpreter->registerNativeFunction("_native_log_warn",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (!args.empty())
                {
                    std::string message = extractString(args[0]);
                    if (!message.empty())
                    {
                        vfLogWarning("[Script] {}", message);
                    }
                }
                return value::Value(std::monostate{});
            }});

        interpreter->registerNativeFunction("_native_log_error",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (!args.empty())
                {
                    std::string message = extractString(args[0]);
                    if (!message.empty())
                    {
                        vfLogError("[Script] {}", message);
                    }
                }
                return value::Value(std::monostate{});
            }});
    }
}
