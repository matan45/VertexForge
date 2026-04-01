#pragma once

#include "../../data/VFXTypes.hpp"
#include "weather/WeatherTypes.hpp"

namespace services
{
    class IVFXRuntimeProvider;

    class RainController
    {
    public:
        void init(IVFXRuntimeProvider* provider);
        void cleanup();
        void update(float deltaTime, const weather::WeatherState& state);
        bool isActive() const { return active; }

    private:
        void createRainInstance();
        void destroyRainInstance();
        void applyRainOverrides(const weather::WeatherState& state);
        void setupInitialConfig();

        IVFXRuntimeProvider* vfxProvider = nullptr;
        VFXInstanceId rainInstanceId = 0;
        bool active = false;
        bool initialConfigApplied = false;

        static constexpr float MAX_SPAWN_RATE = 50000.0f;
        static constexpr float BASE_DROPLET_SIZE = 0.03f;
        static constexpr float BASE_FALL_SPEED = 10.0f;
        static constexpr float ACTIVATION_THRESHOLD = 0.01f;
    };
}
