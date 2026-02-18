#pragma once

#include "WaterTypes.hpp"
#include "../math/Frustum.hpp"
#include <glm/glm.hpp>

namespace water
{
    struct WaterTile
    {
        TileCoord coord;
        glm::vec3 worldOrigin{0.0f};
        math::AABB worldBounds;

        float waterHeight = 0.0f;
        float waveIntensity = 1.0f;
        bool physicsEnabled = true;
        bool isVisible = true;

        WaterTile() = default;

        WaterTile(const TileCoord& coord, float tileSize, float height)
            : coord(coord)
            , waterHeight(height)
        {
            worldOrigin = glm::vec3(
                static_cast<float>(coord.x) * tileSize,
                height,
                static_cast<float>(coord.z) * tileSize
            );

            float halfWaveMargin = 2.0f;
            worldBounds = math::AABB(
                glm::vec3(worldOrigin.x, height - halfWaveMargin, worldOrigin.z),
                glm::vec3(worldOrigin.x + tileSize, height + halfWaveMargin, worldOrigin.z + tileSize)
            );
        }

        void updateHeight(float newHeight, float tileSize)
        {
            waterHeight = newHeight;
            worldOrigin.y = newHeight;

            float halfWaveMargin = 2.0f;
            worldBounds = math::AABB(
                glm::vec3(worldOrigin.x, newHeight - halfWaveMargin, worldOrigin.z),
                glm::vec3(worldOrigin.x + tileSize, newHeight + halfWaveMargin, worldOrigin.z + tileSize)
            );
        }
    };
}
