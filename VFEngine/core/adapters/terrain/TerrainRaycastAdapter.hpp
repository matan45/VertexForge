#pragma once

#include "../../services/providers/terrain/ITerrainRaycastProvider.hpp"

namespace controllers
{
    class OffScreen;
}

namespace core
{
    class TerrainRaycastAdapter : public services::ITerrainRaycastProvider
    {
    private:
        controllers::OffScreen& offScreen;

    public:
        explicit TerrainRaycastAdapter(controllers::OffScreen& offScreen);
        ~TerrainRaycastAdapter() override = default;

        void setRaycastCursorUV(const glm::vec2& uv) override;
        void clearRaycastCursor() override;
        terrain::TerrainHitResult getTerrainHitResult() const override;
        void setBrushOverlayParams(float radius, float falloff, float shape, float stampRotation = 0.0f) override;
    };
}
