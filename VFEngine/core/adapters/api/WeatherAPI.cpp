// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>

#include "WeatherAPI.hpp"
#include "NativeHelpers.hpp"
#include "events/EventDispatcher.hpp"
#include "events/weather/WeatherEvents.hpp"
#include "weather/WeatherTypes.hpp"
#include "weather/WeatherPresets.hpp"
#include <optional>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace
{
    std::optional<weather::WeatherPresetId> stringToPresetId(const std::string& name)
    {
        if (name == "Clear")        return weather::WeatherPresetId::Clear;
        if (name == "Cloudy")       return weather::WeatherPresetId::Cloudy;
        if (name == "Overcast")     return weather::WeatherPresetId::Overcast;
        if (name == "LightRain")    return weather::WeatherPresetId::LightRain;
        if (name == "HeavyRain")    return weather::WeatherPresetId::HeavyRain;
        if (name == "Thunderstorm") return weather::WeatherPresetId::Thunderstorm;
        if (name == "LightSnow")    return weather::WeatherPresetId::LightSnow;
        if (name == "HeavySnow")    return weather::WeatherPresetId::HeavySnow;
        if (name == "Fog")          return weather::WeatherPresetId::Fog;
        if (name == "Sandstorm")    return weather::WeatherPresetId::Sandstorm;
        return std::nullopt;
    }
}

namespace core::api
{
    void WeatherAPI::registerAPI(services::ScriptInterpreter* interpreter)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        // Weather.setWeather(presetName, transitionDuration)
        interpreter->registerNativeFunction("_native_weather_setWeather",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 2) return value::Value(std::monostate{});

                std::string presetName = extractString(args[0], "Weather.setWeather");
                float duration = extractFloat(args[1], "Weather.setWeather");

                auto presetId = stringToPresetId(presetName);
                if (!presetId.has_value())
                {
                    vfLogWarning("[WeatherAPI] Unknown preset: {}", presetName);
                    return value::Value(std::monostate{});
                }

                events::weather::SetWeatherPresetCommand cmd;
                cmd.preset = presetId.value();
                cmd.transitionDuration = duration;
                cmd.easing = weather::WeatherEasing::EaseInOut;
                dispatcher.execute(cmd);

                return value::Value(std::monostate{});
            });

        // Weather.getCurrentWeather() -> float[15]
        interpreter->registerNativeFunction("_native_weather_getCurrentWeather",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                try
                {
                    auto state = dispatcher.query(events::weather::GetWeatherStateQuery{});
                    return makeWeatherStateArray(state);
                }
                catch (...)
                {
                    return value::Value(std::monostate{});
                }
            });

        // Weather.getSnowAccumulation() -> float
        interpreter->registerNativeFunction("_native_weather_getSnowAccumulation",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                try
                {
                    float acc = dispatcher.query(events::weather::GetSnowAccumulationQuery{});
                    return value::Value(acc);
                }
                catch (...)
                {
                    return value::Value(0.0);
                }
            });

        // Weather.getWindDirection() -> float[3] (Vec3)
        interpreter->registerNativeFunction("_native_weather_getWindDirection",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                try
                {
                    auto state = dispatcher.query(events::weather::GetWeatherStateQuery{});
                    float rad = state.windDirectionDeg * static_cast<float>(M_PI) / 180.0f;
                    glm::vec3 dir(std::cos(rad), 0.0f, std::sin(rad));
                    return makeVec3Array(dir);
                }
                catch (...)
                {
                    return makeVec3Array(glm::vec3(0.0f));
                }
            });

        // Weather.getWindSpeed() -> float
        interpreter->registerNativeFunction("_native_weather_getWindSpeed",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                try
                {
                    auto state = dispatcher.query(events::weather::GetWeatherStateQuery{});
                    return value::Value(state.windSpeed);
                }
                catch (...)
                {
                    return value::Value(0.0);
                }
            });

        // Weather.setWeatherScheduleEnabled(bool)
        interpreter->registerNativeFunction("_native_weather_setWeatherScheduleEnabled",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty()) return value::Value(std::monostate{});

                bool enabled = extractBool(args[0], "Weather.setWeatherScheduleEnabled");
                events::weather::SetWeatherScheduleEnabledCommand cmd;
                cmd.enabled = enabled;
                dispatcher.execute(cmd);

                return value::Value(std::monostate{});
            });
    }
}
