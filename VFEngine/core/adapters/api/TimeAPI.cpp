// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>
#include <environment/NativeContext.hpp>
#include <span>

#include "TimeAPI.hpp"
#include "NativeHelpers.hpp"

#include "time/Timer.hpp"
#include "print/Log.hpp"

namespace core::api
{
    void TimeAPI::registerAPI(services::ScriptInterpreter* interpreter)
    {
        interpreter->registerNativeFunction("_native_time_delta",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                return value::Value(engineTime::Timer::getGameDeltaTime());
            }});

        interpreter->registerNativeFunction("_native_time_unscaledDelta",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                return value::Value(engineTime::Timer::getDeltaTime());
            }});

        interpreter->registerNativeFunction("_native_time_scale",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                return value::Value(engineTime::Timer::getTimeScale());
            }});

        interpreter->registerNativeFunction("_native_time_setScale",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                if (args.empty())
                    return value::Value(std::monostate{});

                double s = static_cast<double>(extractFloat(args[0]));
                if (s < 0.0)
                    s = 0.0;
                engineTime::Timer::setTimeScale(s);
                return value::Value(std::monostate{});
            }});

        interpreter->registerNativeFunction("_native_time_freeze",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                engineTime::Timer::freeze();
                return value::Value(std::monostate{});
            }});

        interpreter->registerNativeFunction("_native_time_unfreeze",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                engineTime::Timer::unfreeze();
                return value::Value(std::monostate{});
            }});

        interpreter->registerNativeFunction("_native_time_isFrozen",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                return value::Value(engineTime::Timer::isFrozen());
            }});

        vfLogInfo("[TimeAPI] Registered Time native functions");
    }
}
