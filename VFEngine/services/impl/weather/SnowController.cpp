#include "SnowController.hpp"
#include "../../providers/vfx/IVFXRuntimeProvider.hpp"
#include <glm/glm.hpp>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace services
{
    void SnowController::init(IVFXRuntimeProvider* provider)
    {
        vfxProvider = provider;
    }

    void SnowController::cleanup()
    {
        if (active)
            destroySnowInstance();
        vfxProvider = nullptr;
    }

    void SnowController::update(float deltaTime, const weather::WeatherState& state)
    {
        if (!vfxProvider || !vfxProvider->isInitialized())
            return;

        bool shouldBeActive = state.precipIntensity > ACTIVATION_THRESHOLD;

        if (shouldBeActive && !active)
        {
            createSnowInstance();
        }
        else if (!shouldBeActive && active)
        {
            destroySnowInstance();
            return;
        }

        if (active)
        {
            applySnowOverrides(state);
        }
    }

    void SnowController::createSnowInstance()
    {
        if (!vfxProvider)
            return;

        VFXRuntimeParams params;
        params.vfxAssetPath = "";
        params.loop = true;
        params.priority = VFXEmitterPriority::High;
        params.cameraRelative = true;

        snowInstanceId = vfxProvider->createInstance(params);
        vfxProvider->playInstance(snowInstanceId);
        active = true;
        initialConfigApplied = false;
    }

    void SnowController::destroySnowInstance()
    {
        if (vfxProvider && snowInstanceId != 0)
        {
            vfxProvider->destroyInstance(snowInstanceId);
        }
        snowInstanceId = 0;
        active = false;
        initialConfigApplied = false;
    }

    void SnowController::applySnowOverrides(const weather::WeatherState& state)
    {
        if (!vfxProvider || snowInstanceId == 0)
            return;

        if (!initialConfigApplied)
        {
            setupInitialConfig();
            initialConfigApplied = true;
        }

        VFXEmitterOverrides overrides;

        // Dynamic overrides based on weather state
        overrides.spawnRate = MAX_SPAWN_RATE * state.precipIntensity;
        overrides.startSize = BASE_FLAKE_SIZE * glm::mix(0.6f, 1.4f, state.precipIntensity);

        // Snow is more affected by wind than rain
        float windRad = state.windDirectionDeg * static_cast<float>(M_PI) / 180.0f;
        overrides.windDirection = glm::vec3(std::cos(windRad), 0.0f, std::sin(windRad));
        overrides.windStrength = state.windSpeed * 0.6f;

        vfxProvider->applyInstanceOverrides(snowInstanceId, overrides);
    }

    void SnowController::setupInitialConfig()
    {
        if (!vfxProvider || snowInstanceId == 0)
            return;

        VFXEmitterOverrides initial;

        // Snowflakes fall downward, slowly
        initial.emitDirection = glm::vec3(0.0f, -1.0f, 0.0f);
        initial.startSpeed = BASE_FALL_SPEED;
        initial.lifetime = 5.0f;
        initial.startColor = glm::vec4(1.0f, 1.0f, 1.0f, 0.9f);

        // Billboard mode - snowflakes are round, not stretched
        initial.renderMode = 0;  // Billboard
        initial.stretchMultiplier = 1.0f;
        initial.softParticleDistance = 1.0f;
        initial.lightingInfluence = 0.3f;

        // Terrain collision - snow lingers briefly
        initial.collisionEnabled = true;
        initial.collisionLifetimeLoss = 0.8f;

        // Low gravity - snow drifts
        initial.gravityDirection = glm::vec3(0.0f, -1.0f, 0.0f);
        initial.gravityStrength = 2.5f;

        // Wider spawn volume to avoid pop-in with slow fall
        initial.shapeDimensions = glm::vec3(25.0f, 15.0f, 25.0f);

        vfxProvider->applyInstanceOverrides(snowInstanceId, initial);
    }
}
