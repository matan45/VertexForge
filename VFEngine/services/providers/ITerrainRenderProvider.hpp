#pragma once

#include <vector>
#include <glm/glm.hpp>

namespace math
{
    class Frustum;
}

namespace terrain
{
    class TerrainTile;
}

namespace services
{
    // Provider interface for terrain rendering in the GPU-driven pipeline.
    // Bridges TerrainService (which owns terrain data) to the graphics layer.
    class ITerrainRenderProvider
    {
    public:
        virtual ~ITerrainRenderProvider() = default;

        // Get visible terrain tiles for GPU rendering
        // Returns raw TerrainTile pointers needed by TerrainGPUAdapter
        virtual std::vector<terrain::TerrainTile*> getVisibleTiles(
            const math::Frustum& frustum,
            const glm::vec3& cameraPosition) = 0;

        // Update LODs for all terrain grids based on camera position
        virtual void updateLODs(const glm::vec3& cameraPosition) = 0;

        // Check if any terrain exists in the scene
        virtual bool hasActiveTerrain() const = 0;

        // Get total tile count across all terrains
        virtual size_t getTotalTileCount() const = 0;
    };
}
