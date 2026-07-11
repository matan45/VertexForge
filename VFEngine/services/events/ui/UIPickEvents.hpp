#pragma once
#include "../EventTypes.hpp"
#include "../../data/EntityHandle.hpp"
#include "../../data/DTOs.hpp"
#include <glm/glm.hpp>
#include <optional>
#include <vector>

namespace events::ui {

    // ============================================
    // Editor viewport UI picking (edit mode only)
    // ============================================

    // Ray-pick the front-most visible UI element under the editor cursor.
    // UI is rendered as world-space quads in edit mode, so the hit test is
    // a 3D ray vs. each element's canvas quad (nearest hit wins; smallest
    // area breaks coplanar ties).
    struct PickUIEntityAtQuery : IQuery<std::optional<services::EntityHandle>> {
        glm::vec2 screenPos;     // absolute mouse position (screen coords)
        glm::vec2 viewportPos;   // viewport content top-left (screen coords)
        glm::vec2 viewportSize;  // viewport content size in pixels
        glm::mat4 viewMatrix;
        glm::mat4 projMatrix;    // editor projection (Vulkan Y-flip applied)

        std::string_view getName() const override { return "PickUIEntityAt"; }
    };

    // World-space corners of a UI element's edit-mode quad, for the editor
    // selection outline. Empty if the entity is not a rendered UI element.
    struct GetUIEntityWorldQuadQuery : IQuery<std::optional<services::UIQuadCorners>> {
        services::EntityHandle entity;

        std::string_view getName() const override { return "GetUIEntityWorldQuad"; }
    };

    // VK-1490: region variant of PickUIEntityAtQuery for the editor marquee —
    // every visible UI element whose edit-mode world quad intersects the
    // screen-rect region frustum. Corners are absolute screen coords in any
    // order (normalized inside, matching buildScreenRegionFrustum).
    struct PickUIEntitiesInRegionQuery : IQuery<std::vector<services::EntityHandle>> {
        glm::vec2 minPx;
        glm::vec2 maxPx;
        glm::vec2 viewportPos;   // viewport content top-left (screen coords)
        glm::vec2 viewportSize;  // viewport content size in pixels
        glm::mat4 viewMatrix;
        glm::mat4 projMatrix;    // editor projection (Vulkan Y-flip applied)

        std::string_view getName() const override { return "PickUIEntitiesInRegion"; }
    };
}
