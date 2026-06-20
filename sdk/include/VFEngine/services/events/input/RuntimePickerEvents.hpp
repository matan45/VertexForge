#pragma once

#include "../EventTypes.hpp"
#include "../../providers/input/IRuntimePickerProvider.hpp" // services::PickRay, services::RaycastHit
#include "../../data/EntityHandle.hpp"                       // services::EntityHandle
#include <glm/glm.hpp>
#include <optional>
#include <vector>
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

    // All entities whose center falls inside the camera frustum of the given
    // screen sub-rectangle (RTS drag-select). minPx/maxPx are viewport pixels in
    // any corner order; the rect is normalized engine-side. Returns an empty
    // vector if there is no primary camera/viewport.
    struct PickRegionQuery : ::events::IQuery<std::vector<services::EntityHandle>>
    {
        glm::vec2 minPx{0.0f};
        glm::vec2 maxPx{0.0f};
        uint16_t layerMask = 0xFFFF;
        std::string_view getName() const override { return "PickRegion"; }
    };
}
