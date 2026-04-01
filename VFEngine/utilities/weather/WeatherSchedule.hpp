#pragma once

#include "WeatherTypes.hpp"
#include "WeatherPresets.hpp"
#include <nlohmann/json.hpp>
#include <vector>
#include <string>
#include <optional>
#include <random>

namespace weather
{
    struct WeatherScheduleEntry
    {
        WeatherPresetId preset = WeatherPresetId::Clear;
        float weight = 1.0f;
        float minDuration = 60.0f;     // seconds
        float maxDuration = 300.0f;    // seconds
        float transitionDuration = 60.0f;
        WeatherEasing easing = WeatherEasing::EaseInOut;
    };

    struct TimeOfDayBucket
    {
        float startHour = 0.0f;   // 0-24
        float endHour = 24.0f;    // 0-24
        std::vector<WeatherScheduleEntry> entries;
    };

    struct BiomeWeatherSchedule
    {
        std::string biomeId;
        std::vector<TimeOfDayBucket> buckets;
    };

    class WeatherSchedule
    {
    public:
        void loadFromJson(const nlohmann::json& j);
        nlohmann::json toJson() const;

        void setActiveBiome(const std::string& biomeId);
        const std::string& getActiveBiome() const { return activeBiomeId; }
        void setManualOverride(bool active) { manualOverrideActive = active; }
        bool isManualOverride() const { return manualOverrideActive; }

        std::optional<WeatherTransition> evaluate(float timeOfDay, float deltaTime);

    private:
        BiomeWeatherSchedule parseBiome(const nlohmann::json& biomeJson) const;
        WeatherScheduleEntry parseEntry(const nlohmann::json& entryJson) const;
        const BiomeWeatherSchedule* findActiveBiome() const;
        const TimeOfDayBucket* findBucket(const BiomeWeatherSchedule& biome, float timeOfDay) const;
        WeatherScheduleEntry selectWeightedRandom(const std::vector<WeatherScheduleEntry>& entries);

        std::vector<BiomeWeatherSchedule> biomes;
        std::string activeBiomeId;
        float timeUntilNextChange = 0.0f;
        bool manualOverrideActive = false;
        std::mt19937 rng{std::random_device{}()};
    };
}
