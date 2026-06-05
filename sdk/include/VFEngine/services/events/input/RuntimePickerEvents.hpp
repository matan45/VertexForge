#pragma once

#include "../EventTypes.hpp"
#include "../../providers/input/IRuntimePickerProvider.hpp" // services::PickRay, services::RaycastHit
#include <glm/glm.hpp>
#include <optional>
#include <cstdint>

namespace events::input
{
    // Screen pixel -> world ray via the primary camera. nullopt if no primary camera.
    struct ScreenToWorldRayQuery : ::events::IQuery<std::optional<services::PickRay>>
    {
        glm::vec2 screenPos{0.0f};
        std::string_view getName() const override { return "ScreenToWorldRay"; }
    };

    // World position -> viewport pixel via the primary camera (inverse of
    // ScreenToWorldRayQuery). nullopt if no primary camera or behind the camera.
    struct WorldToScreenQuery : ::events::IQuery<std::optional<glm::vec2>>
    {
        glm::vec3 worldPos{0.0f};
        std::string_view getName() const override { return "WorldToScreen"; }
    };

    // Physics raycast against entities under the given screen pixel. hit=false on miss.
    struct PickEntityQuery : ::events::IQuery<services::RaycastHit>
    {
        glm::vec2 screenPos{0.0f};
        uint16_t layerMask = 0xFFFF;
        std::string_view getName() const override { return "PickEntity"; }
    };
}
