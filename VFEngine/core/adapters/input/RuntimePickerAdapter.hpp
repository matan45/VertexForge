#pragma once

#include "../../../services/providers/input/IRuntimePickerProvider.hpp"

namespace core
{
    // Implements IRuntimePickerProvider by composing existing CQRS queries:
    //   - GetPrimaryCameraQuery + CameraComponent matrices (screen ray)
    //   - GetViewportWidth/HeightQuery (NDC conversion)
    //   - RaycastQuery (entity / terrain "Static" pick)
    // No Core controller dependency; created by the bootstrap and consumed by
    // RuntimePickerServiceImpl.
    class RuntimePickerAdapter : public services::IRuntimePickerProvider
    {
    public:
        bool screenToWorldRay(const glm::vec2& screenPos, services::PickRay& outRay) override;
        services::RaycastHit pickEntity(const services::PickRay& ray, uint16_t layerMask) override;
    };
}
