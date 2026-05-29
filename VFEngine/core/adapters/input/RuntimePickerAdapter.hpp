#pragma once

#include "../../../services/providers/input/IRuntimePickerProvider.hpp"

namespace core
{
    // Implements IRuntimePickerProvider by composing existing CQRS queries:
    //   - GetPrimaryCameraQuery + CameraComponent matrices (screen ray)
    //   - GetViewportWidth/HeightQuery (NDC conversion)
    //   - GetTerrainHeightAtQuery (terrain ray-march)
    //   - RaycastQuery (entity pick)
    // No Core controller dependency; created by the bootstrap and consumed by
    // RuntimePickerServiceImpl.
    class RuntimePickerAdapter : public services::IRuntimePickerProvider
    {
    public:
        bool screenToWorldRay(const glm::vec2& screenPos, services::PickRay& outRay) override;
        std::optional<glm::vec3> pickTerrain(const services::PickRay& ray) override;
        services::RaycastHit pickEntity(const services::PickRay& ray, uint16_t layerMask) override;
    };
}
