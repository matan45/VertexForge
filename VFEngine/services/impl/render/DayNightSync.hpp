#pragma once
#include "../../providers/render/IOffScreenProvider.hpp"
#include "atmosphere/DayNightCycleController.hpp"
#include "atmosphere/SunEntitySync.hpp"
#include "scene/EntityRegistry.hpp"
#include "scene/Entity.hpp"
#include "components/Components.hpp"

namespace services
{
    // VK-1566: advance the atmosphere day-night cycle by deltaTime on the provider's live
    // settings and, when AtmosphereSettings::cycleControlsSunEntity is set, rotate the first
    // effectively-active directional-light entity (the sun) so its local -Z (light-travel
    // direction) tracks the sun. This keeps the sky, the sun light color, and the VSM shadows
    // reading the same freshly-advanced sun angles within a single frame.
    //
    // Called from the "SunSync" frame-graph step (Editor + Runtime) which is ordered BEFORE
    // the "Transforms" step, so the rotation is baked into worldMatrix before shadows/lighting
    // read it. No-op unless atmosphere and the day-night cycle are both enabled -> bit-identical
    // to legacy behavior when the feature is off.
    inline void tickDayNightAndSyncSun(IOffScreenProvider* provider, float deltaTime)
    {
        if (!provider)
            return;
        // Match the guard from the removed AtmospherePipeline::updateDayNightCycle: skip pauses,
        // the first frame, and large hitches.
        if (!(deltaTime > 0.0f && deltaTime < 1.0f))
            return;

        auto settings = provider->getAtmosphereSettings();
        if (!settings.enabled || !settings.dayNightEnabled)
            return;

        render::atmosphere::DayNightCycleController controller;
        controller.tick(deltaTime, settings);        // advance timeOfDay + sun/moon angles
        provider->applyAtmosphereSettings(settings); // push fresh angles to the sky pipeline

        if (!settings.cycleControlsSunEntity)
            return;

        // Rotate the first effectively-active directional light (the sun; index 0 in the GPU
        // light buffer). Iterate the SAME view + active check as
        // GPULightBufferManager::collectDirectionalLights so this targets exactly the entity the
        // buffer treats as the sun. Going through the entity transform - not a GPU-buffer
        // direction override - is required because VSM shadow views read
        // WorldTransformComponent.worldMatrix.
        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::DirectionalLightComponent, components::WorldTransformComponent>();
        for (auto entity : view)
        {
            if (!scene::Entity::isEffectivelyActive(registry, entity))
                continue;
            auto* transform = registry.try_get<components::TransformComponent>(entity);
            if (!transform)
                continue;
            transform->rotation = render::atmosphere::directionalLightEulerForSun(settings.sunAzimuth,
                                                                                  settings.sunElevation);
            transform->isDirty = true; // SceneGraphSystem re-bakes worldMatrix for dirty entities
            break;
        }
    }
}
