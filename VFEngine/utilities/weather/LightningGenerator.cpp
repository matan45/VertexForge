#include "LightningGenerator.hpp"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace weather
{
    void LightningGenerator::update(float deltaTime, const WeatherState& state, const glm::vec3& cameraPos)
    {
        // Only active during thunderstorms
        bool isThunderstorm = state.precipType == PrecipitationType::Rain
                           && state.precipIntensity >= THUNDERSTORM_THRESHOLD;

        if (!isThunderstorm)
        {
            reset();
            return;
        }

        // Clear per-frame output flags
        currentOutput.shouldPlayThunder = false;
        currentOutput.flashIntensity = 0.0f;

        // Update active strike
        if (activeStrike.has_value())
        {
            auto& strike = activeStrike.value();
            strike.timeSinceStrike += deltaTime;

            // Flash decay: sharp exponential
            if (strike.timeSinceStrike < FLASH_DURATION)
            {
                currentOutput.flashIntensity = std::exp(-strike.timeSinceStrike / FLASH_DECAY_RATE);
            }

            // Thunder trigger: delayed by distance / speed of sound
            if (!strike.thunderTriggered && strike.timeSinceStrike >= strike.thunderDelay)
            {
                strike.thunderTriggered = true;
                currentOutput.shouldPlayThunder = true;
                currentOutput.thunderPosition = strike.position;
                currentOutput.thunderSoundIndex = nextSoundIndex;
                nextSoundIndex = (nextSoundIndex + 1) % THUNDER_SOUND_COUNT;
            }

            // Clear strike when both flash done and thunder dispatched
            if (strike.timeSinceStrike >= FLASH_DURATION && strike.thunderTriggered)
            {
                activeStrike.reset();
            }
        }

        // Count down to next strike
        if (!activeStrike.has_value())
        {
            timeUntilNextStrike -= deltaTime;
            if (timeUntilNextStrike <= 0.0f)
            {
                generateStrike(cameraPos);

                std::uniform_real_distribution<float> intervalDist(MIN_INTERVAL, MAX_INTERVAL);
                timeUntilNextStrike = intervalDist(rng);
            }
        }
    }

    void LightningGenerator::reset()
    {
        activeStrike.reset();
        currentOutput = LightningOutput{};
        timeUntilNextStrike = 0.0f;
    }

    void LightningGenerator::generateStrike(const glm::vec3& cameraPos)
    {
        std::uniform_real_distribution<float> distDist(MIN_STRIKE_DIST, MAX_STRIKE_DIST);
        std::uniform_real_distribution<float> angleDist(0.0f, static_cast<float>(2.0 * M_PI));

        float distance = distDist(rng);
        float angle = angleDist(rng);

        LightningStrike strike;
        strike.position = cameraPos + glm::vec3(
            std::cos(angle) * distance,
            STRIKE_ALTITUDE,
            std::sin(angle) * distance
        );
        strike.timeSinceStrike = 0.0f;
        strike.thunderDelay = distance / SPEED_OF_SOUND;
        strike.thunderTriggered = false;

        activeStrike = strike;

        // Set initial flash
        currentOutput.flashIntensity = 1.0f;
    }
}
