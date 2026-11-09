#pragma once
#include "../../providers/render/IOffScreenProvider.hpp"
#include "atmosphere/DayNightCycleController.hpp"
#include "atmosphere/SunEntitySync.hpp"
#include "scene/EntityRegistry.hpp"
#include "scene/Entity.hpp"
#include "components/Components.hpp"
#include <optional>

namespace services
{
    // VK-1654: the directional light the renderer treats as the sun.
    //
    // activeOnly=true returns the FIRST effectively-active directional light. It iterates the SAME
    // view + active check as GPULightBufferManager::collectDirectionalLights, so it is exactly the
    // entity the GPU light buffer treats as the sun (index 0). activeOnly=false returns the first
    // DirectionalLightComponent in any state - the existence check behind the editor's
    // auto-created "Sun" (EditorRenderServiceImpl::registerAtmosphereHandlers).
    [[nodiscard]] inline std::optional<entt::entity> findSunEntity(entt::registry& registry, bool activeOnly = true)
    {
        if (!activeOnly)
        {
            auto view = registry.view<components::DirectionalLightComponent>();
            if (view.begin() == view.end())
                return std::nullopt;
            return *view.begin();
        }

        auto view = registry.view<components::DirectionalLightComponent, components::WorldTransformComponent>();
        for (auto entity : view)
        {
            if (scene::Entity::isEffectivelyActive(registry, entity))
                return entity;
        }
        return std::nullopt;
    }

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
        // light buffer). findSunEntity iterates the SAME view + active check as
        // GPULightBufferManager::collectDirectionalLights so this targets exactly the entity the
        // buffer treats as the sun. Going through the entity transform - not a GPU-buffer
        // direction override - is required because VSM shadow views read
        // WorldTransformComponent.worldMatrix.
        auto& registry = scene::EntityRegistry::getRegistry();
        const auto sun = findSunEntity(registry);
        if (!sun)
            return;

        // findSunEntity stops at the FIRST effectively-active directional light whether or not it
        // has a transform. GPULightBufferManager treats that entity as the sun regardless, so
        // skipping past it to the next one would rotate a different light than the one the sky
        // and the sun-color feedback are driving.
        if (auto* transform = registry.try_get<components::TransformComponent>(*sun))
        {
            transform->rotation = ::render::atmosphere::directionalLightEulerForSun(settings.sunAzimuth,
                                                                                    settings.sunElevation);
            transform->isDirty = true; // SceneGraphSystem re-bakes worldMatrix for dirty entities
        }
    }
}
