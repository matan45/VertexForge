#include "WeatherZoneEvaluator.hpp"
#include "../components/WeatherComponents.hpp"
#include "../components/CoreComponents.hpp"
#include "../scene/EntityRegistry.hpp"
#include <glm/gtc/matrix_inverse.hpp>
#include <algorithm>

namespace weather
{
    WeatherState WeatherZoneEvaluator::evaluate(const glm::vec3& cameraPos, const WeatherState& globalState)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::WeatherZoneComponent, components::WorldTransformComponent>();

        std::vector<ActiveZone> activeZones;
        std::vector<uint32_t> currentlyInsideZones;
        transitions.clear();

        for (auto entity : view)
        {
            auto& zone = view.get<components::WeatherZoneComponent>(entity);
            const auto& worldTransform = view.get<components::WorldTransformComponent>(entity);

            if (!zone.active)
                continue;

            glm::vec3 zonePos = glm::vec3(worldTransform.worldMatrix[3]);
            float signedDist = 0.0f;

            if (zone.shape == components::WeatherZoneShape::Sphere)
            {
                signedDist = glm::distance(cameraPos, zonePos) - zone.radius;
            }
            else // Box
            {
                glm::mat4 invWorld = glm::inverse(worldTransform.worldMatrix);
                glm::vec3 localPos = glm::vec3(invWorld * glm::vec4(cameraPos, 1.0f));
                glm::vec3 d = glm::abs(localPos) - zone.halfExtents;
                glm::vec3 clamped = glm::max(d, glm::vec3(0.0f));
                float outside = glm::length(clamped);
                float inside = glm::min(glm::max(d.x, glm::max(d.y, d.z)), 0.0f);
                signedDist = outside + inside;
            }

            // Update runtime state
            bool wasInside = zone.isCameraInside;
            zone.isCameraInside = signedDist <= 0.0f;

            uint32_t entityIdRaw = static_cast<uint32_t>(entity);

            if (zone.isCameraInside)
                currentlyInsideZones.push_back(entityIdRaw);

            // Track enter/exit transitions
            if (zone.isCameraInside && !wasInside)
                transitions.push_back({entityIdRaw, true});
            else if (!zone.isCameraInside && wasInside)
                transitions.push_back({entityIdRaw, false});

            // Only consider zones within falloff range
            if (signedDist < zone.falloffDistance)
            {
                float blendWeight = 1.0f - std::clamp(signedDist / std::max(zone.falloffDistance, 0.001f), 0.0f, 1.0f);
                zone.currentBlendWeight = blendWeight;

                ActiveZone active;
                active.priority = zone.priority;
                active.blendWeight = blendWeight;
                active.state = zone.overrideState;
                active.entityId = entityIdRaw;
                activeZones.push_back(active);
            }
            else
            {
                zone.currentBlendWeight = 0.0f;
            }
        }

        previouslyInsideZones = currentlyInsideZones;

        if (activeZones.empty())
            return globalState;

        // Sort by priority descending - highest priority wins
        std::sort(activeZones.begin(), activeZones.end(),
            [](const ActiveZone& a, const ActiveZone& b) { return a.priority > b.priority; });

        // Blend highest priority zone with global state
        const auto& topZone = activeZones[0];
        return lerpWeatherState(globalState, topZone.state, topZone.blendWeight);
    }
}
