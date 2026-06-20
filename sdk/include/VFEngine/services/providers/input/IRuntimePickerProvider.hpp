#pragma once

#include <glm/glm.hpp>
#include <optional>
#include <vector>
#include <cstdint>
#include "../../interfaces/physics/IPhysicsService.hpp" // services::RaycastHit
#include "../../data/EntityHandle.hpp"                   // services::EntityHandle

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

        // World position -> viewport pixel (inverse of screenToWorldRay). Returns false
        // when there is no primary camera/viewport or the point is behind the camera.
        // outScreen may lie outside the viewport bounds for visible-but-offscreen points.
        virtual bool worldToScreen(const glm::vec3& worldPos, glm::vec2& outScreen) = 0;

        // Physics raycast against scene entities. hit=false on miss.
        virtual RaycastHit pickEntity(const PickRay& ray, uint16_t layerMask) = 0;

        // All entities whose center lies inside the camera frustum of the given
        // screen sub-rectangle (drag-select box). minPx/maxPx are viewport pixels
        // in any corner order. Returns an empty vector when there is no primary
        // camera/viewport. layerMask is accepted for API symmetry but is not
        // applied here (no cheap per-entity physics layer); scripts post-filter.
        virtual std::vector<EntityHandle> pickRegion(const glm::vec2& minPx,
                                                     const glm::vec2& maxPx,
                                                     uint16_t layerMask) = 0;
    };
}
