#include "WeatherZoneEvaluator.hpp"
#include "../scene/EntityRegistry.hpp"
#include <glm/gtc/matrix_inverse.hpp>
#include <algorithm>

namespace weather
{
    float WeatherZoneEvaluator::computeSignedDistance(
        const glm::vec3& cameraPos,
        const components::WeatherZoneComponent& zone,
        const components::WorldTransformComponent& transform)
    {
        glm::vec3 zonePos = glm::vec3(transform.worldMatrix[3]);

        if (zone.shape == components::WeatherZoneShape::Sphere)
            return glm::distance(cameraPos, zonePos) - zone.radius;

        glm::mat4 invWorld = glm::inverse(transform.worldMatrix);
        glm::vec3 localPos = glm::vec3(invWorld * glm::vec4(cameraPos, 1.0f));
        glm::vec3 d = glm::abs(localPos) - zone.halfExtents;
        glm::vec3 clamped = glm::max(d, glm::vec3(0.0f));
        float outside = glm::length(clamped);
        float inside = glm::min(glm::max(d.x, glm::max(d.y, d.z)), 0.0f);
        return outside + inside;
    }

    WeatherState WeatherZoneEvaluator::evaluate(const glm::vec3& cameraPos, const WeatherState& globalState)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::WeatherZoneComponent, components::WorldTransformComponent>();

        std::vector<ActiveZone> activeZones;
        transitions.clear();

        for (auto entity : view)
        {
            auto& zone = view.get<components::WeatherZoneComponent>(entity);
            const auto& worldTransform = view.get<components::WorldTransformComponent>(entity);
            if (!zone.active) continue;

            float signedDist = computeSignedDistance(cameraPos, zone, worldTransform);
            uint32_t entityIdRaw = static_cast<uint32_t>(entity);

            bool wasInside = zone.isCameraInside;
            zone.isCameraInside = signedDist <= 0.0f;

            if (zone.isCameraInside && !wasInside)
                transitions.push_back({entityIdRaw, true});
            else if (!zone.isCameraInside && wasInside)
                transitions.push_back({entityIdRaw, false});

            if (signedDist < zone.falloffDistance)
            {
                float weight = 1.0f - std::clamp(signedDist / std::max(zone.falloffDistance, 0.001f), 0.0f, 1.0f);
                zone.currentBlendWeight = weight;
                activeZones.push_back({zone.priority, weight, zone.overrideState, entityIdRaw});
            }
            else
            {
                zone.currentBlendWeight = 0.0f;
            }
        }

        if (activeZones.empty())
            return globalState;

        std::sort(activeZones.begin(), activeZones.end(),
            [](const ActiveZone& a, const ActiveZone& b) { return a.priority > b.priority; });

        return lerpWeatherState(globalState, activeZones[0].state, activeZones[0].blendWeight);
    }
}
