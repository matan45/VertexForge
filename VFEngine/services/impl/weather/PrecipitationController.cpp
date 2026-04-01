#include "PrecipitationController.hpp"
#include "../../providers/vfx/IVFXRuntimeProvider.hpp"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace services
{
    void PrecipitationController::init(IVFXRuntimeProvider* provider)
    {
        vfxProvider = provider;
    }

    void PrecipitationController::cleanup()
    {
        if (active)
            destroyInstance();
        vfxProvider = nullptr;
    }

    void PrecipitationController::update(float deltaTime, const weather::WeatherState& state)
    {
        if (!vfxProvider || !vfxProvider->isInitialized())
            return;

        auto cfg = getConfig();
        bool shouldBeActive = state.precipIntensity > cfg.activationThreshold;

        if (shouldBeActive && !active)
            createInstance();
        else if (!shouldBeActive && active)
        {
            destroyInstance();
            return;
        }

        if (active)
            applyOverrides(state);
    }

    void PrecipitationController::createInstance()
    {
        if (!vfxProvider) return;

        VFXRuntimeParams params;
        params.vfxAssetPath = "";
        params.loop = true;
        params.priority = VFXEmitterPriority::High;
        params.cameraRelative = true;

        instanceId = vfxProvider->createInstance(params);
        vfxProvider->playInstance(instanceId);
        active = true;
        initialConfigApplied = false;
    }

    void PrecipitationController::destroyInstance()
    {
        if (vfxProvider && instanceId != 0)
            vfxProvider->destroyInstance(instanceId);
        instanceId = 0;
        active = false;
        initialConfigApplied = false;
    }

    void PrecipitationController::applyOverrides(const weather::WeatherState& state)
    {
        if (!vfxProvider || instanceId == 0) return;

        if (!initialConfigApplied)
        {
            applyInitialConfig();
            initialConfigApplied = true;
        }

        auto cfg = getConfig();
        VFXEmitterOverrides overrides;
        overrides.spawnRate = cfg.maxSpawnRate * state.precipIntensity;
        overrides.startSize = cfg.baseSize * glm::mix(cfg.sizeIntensityMin, cfg.sizeIntensityMax, state.precipIntensity);
        overrides.stretchMultiplier = glm::mix(cfg.stretchMin, cfg.stretchMax, state.precipIntensity);

        float windRad = state.windDirectionDeg * static_cast<float>(M_PI) / 180.0f;
        overrides.windDirection = glm::vec3(std::cos(windRad), 0.0f, std::sin(windRad));
        overrides.windStrength = state.windSpeed * cfg.windStrengthMult;

        vfxProvider->applyInstanceOverrides(instanceId, overrides);
    }

    void PrecipitationController::applyInitialConfig()
    {
        if (!vfxProvider || instanceId == 0) return;

        auto cfg = getConfig();
        VFXEmitterOverrides initial;
        initial.emitDirection = glm::vec3(0.0f, -1.0f, 0.0f);
        initial.coneSpread = cfg.coneSpread;
        initial.startSpeed = cfg.baseFallSpeed;
        initial.lifetime = cfg.lifetime;
        initial.startColor = cfg.startColor;
        initial.renderMode = cfg.renderMode;
        initial.softParticleDistance = cfg.softParticleDist;
        initial.lightingInfluence = cfg.lightingInfluence;
        initial.collisionEnabled = true;
        initial.collisionLifetimeLoss = cfg.collisionLifetimeLoss;
        initial.gravityDirection = glm::vec3(0.0f, -1.0f, 0.0f);
        initial.gravityStrength = cfg.gravityStrength;
        initial.shapeDimensions = cfg.shapeDimensions;

        vfxProvider->applyInstanceOverrides(instanceId, initial);
    }
}
