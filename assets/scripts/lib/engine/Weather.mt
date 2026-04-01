// Weather - Controls the dynamic weather system
//
// Usage:
//   import * from "lib/engine/Weather.mt";
//   Weather::setWeather("HeavyRain", 60.0);
//   float snow = Weather::getSnowAccumulation();
//   float[] wind = Weather::getWindDirection();
//
// Weather state array indices (from getCurrentWeather):
//   [0] cloudCoverage, [1] cloudDensity, [2] cloudType,
//   [3] precipType (0=None, 1=Rain, 2=Snow), [4] precipIntensity,
//   [5] windSpeed, [6] windDirectionDeg, [7] gustStrength, [8] gustFrequency,
//   [9] fogDensity, [10] heightFogDensity, [11] ambientLightMult,
//   [12] atmosphereTintR, [13] atmosphereTintG, [14] atmosphereTintB
//
// Available presets:
//   "Clear", "Cloudy", "Overcast", "LightRain", "HeavyRain",
//   "Thunderstorm", "LightSnow", "HeavySnow", "Fog", "Sandstorm"
//
// Event interface: IWeatherEventListener
//   onWeatherChanged(float[] previousState, float[] newState)
//   onLightningStrike(float[] position, float intensity)
//   onWeatherZoneEntered()
//   onWeatherZoneExited()

public class Weather {
    public constructor() {
    }

    // ============================================
    // Weather Control
    // ============================================

    // Transition to a weather preset over the given duration (seconds)
    public static function setWeather(string presetName, float transitionDuration): void {
        _native_weather_setWeather(presetName, transitionDuration);
    }

    // Get the current interpolated weather state as a 15-element float array
    public static function getCurrentWeather(): float[] {
        return _native_weather_getCurrentWeather();
    }

    // ============================================
    // Queries
    // ============================================

    // Get current snow accumulation level (0.0 = none, 1.0 = full)
    public static function getSnowAccumulation(): float {
        return _native_weather_getSnowAccumulation();
    }

    // Get wind direction as a normalized Vec3 (x, y, z)
    public static function getWindDirection(): float[] {
        return _native_weather_getWindDirection();
    }

    // Get wind speed in m/s
    public static function getWindSpeed(): float {
        return _native_weather_getWindSpeed();
    }

    // ============================================
    // Schedule Control
    // ============================================

    // Enable or disable automatic weather schedule (time-of-day based changes)
    public static function setWeatherScheduleEnabled(bool enabled): void {
        _native_weather_setWeatherScheduleEnabled(enabled);
    }
}
