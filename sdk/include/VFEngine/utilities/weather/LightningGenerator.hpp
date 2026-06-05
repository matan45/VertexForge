#pragma once

#include "WeatherTypes.hpp"
#include <glm/glm.hpp>
#include <optional>
#include <random>

namespace weather
{
    struct LightningStrike
    {
        glm::vec3 position{0.0f};
        float timeSinceStrike = 0.0f;
        float thunderDelay = 0.0f;
        bool thunderTriggered = false;
    };

    struct LightningOutput
    {
        float flashIntensity = 0.0f;
        bool shouldPlayThunder = false;
        glm::vec3 thunderPosition{0.0f};
        int thunderSoundIndex = 0;
    };

    class LightningGenerator
    {
    public:
        void update(float deltaTime, const WeatherState& state, const glm::vec3& cameraPos);
        LightningOutput getOutput() const { return currentOutput; }
        void reset();

    private:
        void generateStrike(const glm::vec3& cameraPos);

        float timeUntilNextStrike = 0.0f;
        std::optional<LightningStrike> activeStrike;
        LightningOutput currentOutput;
        int nextSoundIndex = 0;
        std::mt19937 rng{std::random_device{}()};

        static constexpr float MIN_INTERVAL = 5.0f;
        static constexpr float MAX_INTERVAL = 30.0f;
        static constexpr float FLASH_DURATION = 0.3f;
        static constexpr float FLASH_DECAY_RATE = 0.1f;
        static constexpr float MIN_STRIKE_DIST = 200.0f;
        static constexpr float MAX_STRIKE_DIST = 5000.0f;
        static constexpr float STRIKE_ALTITUDE = 2000.0f;
        static constexpr float SPEED_OF_SOUND = 343.3f;
        static constexpr float THUNDERSTORM_THRESHOLD = 0.9f;
        static constexpr int THUNDER_SOUND_COUNT = 3;
    };
}
