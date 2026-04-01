#include "WeatherSchedule.hpp"
#include <algorithm>
#include <numeric>

namespace weather
{
    WeatherScheduleEntry WeatherSchedule::parseEntry(const nlohmann::json& j) const
    {
        WeatherScheduleEntry entry;
        if (j.contains("preset") && j["preset"].is_number_unsigned())
            entry.preset = static_cast<WeatherPresetId>(j["preset"].get<uint8_t>());
        if (j.contains("weight") && j["weight"].is_number())
            entry.weight = j["weight"].get<float>();
        if (j.contains("minDuration") && j["minDuration"].is_number())
            entry.minDuration = j["minDuration"].get<float>();
        if (j.contains("maxDuration") && j["maxDuration"].is_number())
            entry.maxDuration = j["maxDuration"].get<float>();
        if (j.contains("transitionDuration") && j["transitionDuration"].is_number())
            entry.transitionDuration = j["transitionDuration"].get<float>();
        if (j.contains("easing") && j["easing"].is_number_unsigned())
            entry.easing = static_cast<WeatherEasing>(j["easing"].get<uint8_t>());
        return entry;
    }

    BiomeWeatherSchedule WeatherSchedule::parseBiome(const nlohmann::json& biomeJson) const
    {
        BiomeWeatherSchedule biome;
        if (biomeJson.contains("biomeId") && biomeJson["biomeId"].is_string())
            biome.biomeId = biomeJson["biomeId"].get<std::string>();

        if (biomeJson.contains("buckets") && biomeJson["buckets"].is_array())
        {
            for (const auto& bucketJson : biomeJson["buckets"])
            {
                TimeOfDayBucket bucket;
                if (bucketJson.contains("startHour") && bucketJson["startHour"].is_number())
                    bucket.startHour = bucketJson["startHour"].get<float>();
                if (bucketJson.contains("endHour") && bucketJson["endHour"].is_number())
                    bucket.endHour = bucketJson["endHour"].get<float>();

                if (bucketJson.contains("entries") && bucketJson["entries"].is_array())
                    for (const auto& e : bucketJson["entries"])
                        bucket.entries.push_back(parseEntry(e));

                biome.buckets.push_back(std::move(bucket));
            }
        }
        return biome;
    }

    void WeatherSchedule::loadFromJson(const nlohmann::json& j)
    {
        biomes.clear();
        if (!j.contains("biomes") || !j["biomes"].is_array()) return;

        for (const auto& biomeJson : j["biomes"])
            biomes.push_back(parseBiome(biomeJson));
    }

    nlohmann::json WeatherSchedule::toJson() const
    {
        nlohmann::json j;
        nlohmann::json biomesArray = nlohmann::json::array();

        for (const auto& biome : biomes)
        {
            nlohmann::json biomeJson;
            biomeJson["biomeId"] = biome.biomeId;

            nlohmann::json bucketsArray = nlohmann::json::array();
            for (const auto& bucket : biome.buckets)
            {
                nlohmann::json bucketJson;
                bucketJson["startHour"] = bucket.startHour;
                bucketJson["endHour"] = bucket.endHour;

                nlohmann::json entriesArray = nlohmann::json::array();
                for (const auto& entry : bucket.entries)
                {
                    entriesArray.push_back({
                        {"preset", static_cast<uint8_t>(entry.preset)},
                        {"weight", entry.weight},
                        {"minDuration", entry.minDuration},
                        {"maxDuration", entry.maxDuration},
                        {"transitionDuration", entry.transitionDuration},
                        {"easing", static_cast<uint8_t>(entry.easing)}
                    });
                }
                bucketJson["entries"] = entriesArray;
                bucketsArray.push_back(bucketJson);
            }
            biomeJson["buckets"] = bucketsArray;
            biomesArray.push_back(biomeJson);
        }

        j["biomes"] = biomesArray;
        return j;
    }

    void WeatherSchedule::setActiveBiome(const std::string& biomeId)
    {
        activeBiomeId = biomeId;
    }

    std::optional<WeatherTransition> WeatherSchedule::evaluate(float timeOfDay, float deltaTime)
    {
        if (manualOverrideActive)
            return std::nullopt;

        timeUntilNextChange -= deltaTime;
        if (timeUntilNextChange > 0.0f)
            return std::nullopt;

        const auto* biome = findActiveBiome();
        if (!biome)
            return std::nullopt;

        const auto* bucket = findBucket(*biome, timeOfDay);
        if (!bucket || bucket->entries.empty())
            return std::nullopt;

        auto entry = selectWeightedRandom(bucket->entries);

        // Compute random duration within range
        std::uniform_real_distribution<float> durDist(entry.minDuration, entry.maxDuration);
        timeUntilNextChange = durDist(rng);

        WeatherTransition transition;
        transition.targetState = getPreset(entry.preset);
        transition.duration = entry.transitionDuration;
        transition.easing = entry.easing;

        return transition;
    }

    const BiomeWeatherSchedule* WeatherSchedule::findActiveBiome() const
    {
        for (const auto& biome : biomes)
        {
            if (biome.biomeId == activeBiomeId)
                return &biome;
        }
        // Fallback to first biome if active not found
        return biomes.empty() ? nullptr : &biomes[0];
    }

    const TimeOfDayBucket* WeatherSchedule::findBucket(const BiomeWeatherSchedule& biome, float timeOfDay) const
    {
        for (const auto& bucket : biome.buckets)
        {
            if (bucket.startHour <= bucket.endHour)
            {
                if (timeOfDay >= bucket.startHour && timeOfDay < bucket.endHour)
                    return &bucket;
            }
            else
            {
                // Wrapping bucket (e.g., 22:00 - 06:00)
                if (timeOfDay >= bucket.startHour || timeOfDay < bucket.endHour)
                    return &bucket;
            }
        }
        return nullptr;
    }

    WeatherScheduleEntry WeatherSchedule::selectWeightedRandom(const std::vector<WeatherScheduleEntry>& entries)
    {
        float totalWeight = 0.0f;
        for (const auto& e : entries)
            totalWeight += e.weight;

        if (totalWeight <= 0.0f)
            return entries[0];

        std::uniform_real_distribution<float> dist(0.0f, totalWeight);
        float roll = dist(rng);

        float cumulative = 0.0f;
        for (const auto& e : entries)
        {
            cumulative += e.weight;
            if (roll <= cumulative)
                return e;
        }

        return entries.back();
    }
}
