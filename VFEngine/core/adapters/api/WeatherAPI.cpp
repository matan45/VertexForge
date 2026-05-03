// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>
#include <environment/NativeContext.hpp>
#include <span>

#include "WeatherAPI.hpp"
#include "NativeHelpers.hpp"
#include "events/EventDispatcher.hpp"
#include "events/weather/WeatherEvents.hpp"
#include "weather/WeatherTypes.hpp"
#include "weather/WeatherPresets.hpp"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace core::api
{
    void WeatherAPI::registerAPI(services::ScriptInterpreter* interpreter)
    {
        auto& dispatcher = events::EventDispatcher::instance();
        registerControlFunctions(interpreter, dispatcher);
        registerQueryFunctions(interpreter, dispatcher);
    }

    void WeatherAPI::registerControlFunctions(services::ScriptInterpreter* interpreter,
                                               events::EventDispatcher& dispatcher)
    {
        interpreter->registerNativeFunction("_native_weather_setWeather",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 2) return value::Value(std::monostate{});

                auto presetId = weather::stringToPresetId(extractString(args[0], "Weather.setWeather"));
                if (!presetId.has_value())
                    return value::Value(std::monostate{});

                events::weather::SetWeatherPresetCommand cmd;
                cmd.preset = presetId.value();
                cmd.transitionDuration = extractFloat(args[1], "Weather.setWeather");
                cmd.easing = weather::WeatherEasing::EaseInOut;
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            }});

        interpreter->registerNativeFunction("_native_weather_setWeatherScheduleEnabled",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.empty()) return value::Value(std::monostate{});
                events::weather::SetWeatherScheduleEnabledCommand cmd;
                cmd.enabled = extractBool(args[0], "Weather.setScheduleEnabled");
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            }});
    }

    void WeatherAPI::registerQueryFunctions(services::ScriptInterpreter* interpreter,
                                             events::EventDispatcher& dispatcher)
    {
        interpreter->registerNativeFunction("_native_weather_getCurrentWeather",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                try { return makeWeatherStateArray(dispatcher.query(events::weather::GetWeatherStateQuery{})); }
                catch (...) { return value::Value(std::monostate{}); }
            }});

        interpreter->registerNativeFunction("_native_weather_getSnowAccumulation",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                try { return value::Value(dispatcher.query(events::weather::GetSnowAccumulationQuery{})); }
                catch (...) { return value::Value(0.0); }
            }});

        interpreter->registerNativeFunction("_native_weather_getWindDirection",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                try
                {
                    auto state = dispatcher.query(events::weather::GetWeatherStateQuery{});
                    float rad = state.windDirectionDeg * static_cast<float>(M_PI) / 180.0f;
                    return makeVec3Array(glm::vec3(std::cos(rad), 0.0f, std::sin(rad)));
                }
                catch (...) { return makeVec3Array(glm::vec3(0.0f)); }
            }});

        interpreter->registerNativeFunction("_native_weather_getWindSpeed",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                try { return value::Value(dispatcher.query(events::weather::GetWeatherStateQuery{}).windSpeed); }
                catch (...) { return value::Value(0.0); }
            }});
    }
}
