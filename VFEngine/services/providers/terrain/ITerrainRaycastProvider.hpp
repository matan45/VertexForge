#pragma once

#include <glm/glm.hpp>
#include "terrain/TerrainHitResult.hpp"

namespace services
{
    class ITerrainRaycastProvider
    {
    public:
        virtual ~ITerrainRaycastProvider() = default;

        virtual void setRaycastCursorUV(const glm::vec2& uv) = 0;
        virtual void clearRaycastCursor() = 0;
        virtual terrain::TerrainHitResult getTerrainHitResult() const = 0;
        virtual void setBrushOverlayParams(float radius, float falloff, float shape, float stampRotation = 0.0f) = 0;
    };
}
