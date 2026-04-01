#include "RainController.hpp"
#include <glm/glm.hpp>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace services
{
    void RainController::init(IVFXRuntimeProvider* provider)
    {
        vfxProvider = provider;
    }

    void RainController::cleanup()
    {
        if (active)
            destroyRainInstance();
        vfxProvider = nullptr;
    }

    void RainController::update(float deltaTime, const weather::WeatherState& state)
    {
        if (!vfxProvider || !vfxProvider->isInitialized())
            return;

        bool shouldBeActive = state.precipIntensity > ACTIVATION_THRESHOLD;

        if (shouldBeActive && !active)
        {
            createRainInstance();
        }
        else if (!shouldBeActive && active)
        {
            destroyRainInstance();
            return;
        }

        if (active)
        {
            applyRainOverrides(state);
        }
    }

    void RainController::createRainInstance()
    {
        if (!vfxProvider)
            return;

        VFXRuntimeParams params;
        params.vfxAssetPath = "";  // Use default config, we override everything
        params.loop = true;
        params.priority = VFXEmitterPriority::High;
        params.cameraRelative = true;

        rainInstanceId = vfxProvider->createInstance(params);
        active = true;
        initialConfigApplied = false;
    }

    void RainController::destroyRainInstance()
    {
        if (vfxProvider && rainInstanceId != 0)
        {
            vfxProvider->destroyInstance(rainInstanceId);
        }
        rainInstanceId = 0;
        active = false;
        initialConfigApplied = false;
    }

    void RainController::applyRainOverrides(const weather::WeatherState& state)
    {
        if (!vfxProvider || rainInstanceId == 0)
            return;

        VFXEmitterOverrides overrides;

        // Set initial config on first frame
        if (!initialConfigApplied)
        {
            setupInitialConfig();
            initialConfigApplied = true;
        }

        // Dynamic overrides based on weather state
        overrides.spawnRate = MAX_SPAWN_RATE * state.precipIntensity;
        overrides.startSize = BASE_DROPLET_SIZE * glm::mix(0.8f, 1.5f, state.precipIntensity);
        overrides.stretchMultiplier = glm::mix(2.0f, 5.0f, state.precipIntensity);

        // Wind deflection
        float windRad = state.windDirectionDeg * static_cast<float>(M_PI) / 180.0f;
        overrides.windDirection = glm::vec3(std::cos(windRad), 0.0f, std::sin(windRad));
        overrides.windStrength = state.windSpeed * 0.3f;

        vfxProvider->applyInstanceOverrides(rainInstanceId, overrides);
    }

    void RainController::setupInitialConfig()
    {
        if (!vfxProvider || rainInstanceId == 0)
            return;

        VFXEmitterOverrides initial;

        // Rain falls downward
        initial.emitDirection = glm::vec3(0.0f, -1.0f, 0.0f);
        initial.startSpeed = BASE_FALL_SPEED;
        initial.lifetime = 2.0f;
        initial.startColor = glm::vec4(0.7f, 0.75f, 0.8f, 0.3f);

        // Stretched billboard for rain streaks
        initial.renderMode = 1;  // StretchedBillboard
        initial.softParticleDistance = 0.5f;
        initial.lightingInfluence = 0.2f;

        // Terrain collision - rain dies on hit
        initial.collisionEnabled = true;
        initial.collisionLifetimeLoss = 1.0f;

        // Gravity
        initial.gravityDirection = glm::vec3(0.0f, -1.0f, 0.0f);
        initial.gravityStrength = 9.81f;

        // Box spawn volume: 40x20x40 meters around camera
        initial.shapeDimensions = glm::vec3(20.0f, 10.0f, 20.0f);

        vfxProvider->applyInstanceOverrides(rainInstanceId, initial);
    }
}
