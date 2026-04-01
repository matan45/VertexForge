#pragma once

#include "../../data/VFXTypes.hpp"
#include "weather/WeatherTypes.hpp"

namespace services
{
    class IVFXRuntimeProvider;

    class SnowController
    {
    public:
        void init(IVFXRuntimeProvider* provider);
        void cleanup();
        void update(float deltaTime, const weather::WeatherState& state);
        bool isActive() const { return active; }

    private:
        void createSnowInstance();
        void destroySnowInstance();
        void applySnowOverrides(const weather::WeatherState& state);
        void setupInitialConfig();

        IVFXRuntimeProvider* vfxProvider = nullptr;
        VFXInstanceId snowInstanceId = 0;
        bool active = false;
        bool initialConfigApplied = false;

        static constexpr float MAX_SPAWN_RATE = 30000.0f;
        static constexpr float BASE_FLAKE_SIZE = 0.04f;
        static constexpr float BASE_FALL_SPEED = 3.0f;
        static constexpr float ACTIVATION_THRESHOLD = 0.01f;
    };
}
