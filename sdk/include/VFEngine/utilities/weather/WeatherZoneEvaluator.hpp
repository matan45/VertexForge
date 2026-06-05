#pragma once

#include "WeatherTypes.hpp"
#include "../components/WeatherComponents.hpp"
#include "../components/CoreComponents.hpp"
#include <glm/glm.hpp>
#include <vector>

namespace weather
{
    class WeatherZoneEvaluator
    {
    public:
        struct ZoneTransition
        {
            uint32_t entityId = 0;
            bool entered = false;
        };

        WeatherState evaluate(const glm::vec3& cameraPos, const WeatherState& globalState);
        const std::vector<ZoneTransition>& getTransitions() const { return transitions; }

    private:
        struct ActiveZone
        {
            int priority = 0;
            float blendWeight = 0.0f;
            WeatherState state;
            uint32_t entityId = 0;
        };

        float computeSignedDistance(const glm::vec3& cameraPos,
                                    const components::WeatherZoneComponent& zone,
                                    const components::WorldTransformComponent& transform);

        std::vector<ZoneTransition> transitions;
    };
}
