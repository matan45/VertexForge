#pragma once

#include "../../data/VFXTypes.hpp"
#include "weather/WeatherTypes.hpp"
#include <glm/glm.hpp>

namespace services
{
    class IVFXRuntimeProvider;

    struct PrecipitationConfig
    {
        float maxSpawnRate;
        float baseSize;
        float baseFallSpeed;
        float activationThreshold;
        int renderMode;
        float coneSpread;
        glm::vec4 startColor;
        float lifetime;
        float softParticleDist;
        float lightingInfluence;
        float collisionLifetimeLoss;
        float gravityStrength;
        glm::vec3 shapeDimensions;
        float windStrengthMult;
        float sizeIntensityMin;
        float sizeIntensityMax;
        float stretchMin;
        float stretchMax;
    };

    class PrecipitationController
    {
    public:
        virtual ~PrecipitationController() = default;
        void init(IVFXRuntimeProvider* provider);
        void cleanup();
        void update(float deltaTime, const weather::WeatherState& state);
        bool isActive() const { return active; }

    protected:
        virtual PrecipitationConfig getConfig() const = 0;

    private:
        void createInstance();
        void destroyInstance();
        void applyOverrides(const weather::WeatherState& state);
        void applyInitialConfig();

        IVFXRuntimeProvider* vfxProvider = nullptr;
        VFXInstanceId instanceId = 0;
        bool active = false;
        bool initialConfigApplied = false;
    };
}
