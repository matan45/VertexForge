#pragma once

#include <glm/glm.hpp>
#include <optional>
#include <cstdint>
#include "../../interfaces/physics/IPhysicsService.hpp" // services::RaycastHit

namespace services
{
    // Origin + normalized direction of a world-space picking ray.
    struct PickRay
    {
        glm::vec3 origin{0.0f};
        glm::vec3 direction{0.0f, 0.0f, -1.0f};
    };

    // Runtime screen-ray picking. Implemented by core::RuntimePickerAdapter, which
    // composes the primary camera, active viewport, terrain heightfield and physics
    // raycast. All methods resolve the primary camera + viewport internally so callers
    // only supply a screen-space pixel position.
    class IRuntimePickerProvider
    {
    public:
        virtual ~IRuntimePickerProvider() = default;

        // Screen pixel -> world ray. Returns false if there is no primary camera/viewport.
        virtual bool screenToWorldRay(const glm::vec2& screenPos, PickRay& outRay) = 0;

        // Physics raycast against scene entities. hit=false on miss.
        virtual RaycastHit pickEntity(const PickRay& ray, uint16_t layerMask) = 0;
    };
}
