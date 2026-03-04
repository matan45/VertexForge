#include "TerrainRaycastAdapter.hpp"
#include "../../controllers/OffScreen.hpp"

namespace core
{
    TerrainRaycastAdapter::TerrainRaycastAdapter(controllers::OffScreen& offScreen)
        : offScreen(offScreen)
    {
    }

    void TerrainRaycastAdapter::setRaycastCursorUV(const glm::vec2& uv)
    {
        offScreen.setRaycastCursorUV(uv);
    }

    void TerrainRaycastAdapter::clearRaycastCursor()
    {
        offScreen.clearRaycastCursor();
    }

    terrain::TerrainHitResult TerrainRaycastAdapter::getTerrainHitResult() const
    {
        return offScreen.getTerrainHitResult();
    }

    void TerrainRaycastAdapter::setBrushOverlayParams(float radius, float falloff, float shape)
    {
        offScreen.setBrushOverlayParams(radius, falloff, shape);
    }
}
