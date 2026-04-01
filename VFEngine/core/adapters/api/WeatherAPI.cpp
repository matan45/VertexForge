// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>

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
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
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
            });

        interpreter->registerNativeFunction("_native_weather_setWeatherScheduleEnabled",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty()) return value::Value(std::monostate{});
                events::weather::SetWeatherScheduleEnabledCommand cmd;
                cmd.enabled = extractBool(args[0], "Weather.setScheduleEnabled");
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            });
    }

    void WeatherAPI::registerQueryFunctions(services::ScriptInterpreter* interpreter,
                                             events::EventDispatcher& dispatcher)
    {
        interpreter->registerNativeFunction("_native_weather_getCurrentWeather",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                try { return makeWeatherStateArray(dispatcher.query(events::weather::GetWeatherStateQuery{})); }
                catch (...) { return value::Value(std::monostate{}); }
            });

        interpreter->registerNativeFunction("_native_weather_getSnowAccumulation",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                try { return value::Value(dispatcher.query(events::weather::GetSnowAccumulationQuery{})); }
                catch (...) { return value::Value(0.0); }
            });

        interpreter->registerNativeFunction("_native_weather_getWindDirection",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                try
                {
                    auto state = dispatcher.query(events::weather::GetWeatherStateQuery{});
                    float rad = state.windDirectionDeg * static_cast<float>(M_PI) / 180.0f;
                    return makeVec3Array(glm::vec3(std::cos(rad), 0.0f, std::sin(rad)));
                }
                catch (...) { return makeVec3Array(glm::vec3(0.0f)); }
            });

        interpreter->registerNativeFunction("_native_weather_getWindSpeed",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                try { return value::Value(dispatcher.query(events::weather::GetWeatherStateQuery{}).windSpeed); }
                catch (...) { return value::Value(0.0); }
            });
    }
}
