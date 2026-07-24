#pragma once

// Shared terrain-normal sampler for the brush services (vegetation / mesh / foliage).
//
// All three estimate the surface normal at a world XZ via a 5-tap central difference over
// events::terrain::GetTerrainHeightAtQuery, using the centre height as the fallback for any
// neighbour whose query is invalid or throws. This lived as a byte-identical private method in each
// service; it is shared here so a change to eps / the fallback / the normal convention lands once.
//
// It belongs to the Services layer (it depends on the EventDispatcher + terrain query, both Services)
// — it cannot live in utilities/terrain, which must not depend on Services events. This is distinct
// from terrain::sampleTileNormalCentralDiff (array-based, eps = vertexSpacing*0.5, no fallback), used
// by the scatter bakers on the direct-array path.

#include "../../events/EventDispatcher.hpp"
#include "../../events/terrain/TerrainEvents.hpp"
#include <glm/glm.hpp>

namespace services::brushsampling
{
    inline glm::vec3 sampleTerrainNormalViaHeightQuery(float worldX, float worldZ)
    {
        const float eps = 0.5f;
        auto sample = [](float x, float z, float fallback) -> float {
            events::terrain::GetTerrainHeightAtQuery q;
            q.worldX = x;
            q.worldZ = z;
            try
            {
                auto r = events::EventDispatcher::instance().query(q);
                return r.valid ? r.height : fallback;
            }
            catch (...)
            {
                return fallback;
            }
        };
        float hC = sample(worldX, worldZ, 0.0f);
        float hL = sample(worldX - eps, worldZ, hC);
        float hR = sample(worldX + eps, worldZ, hC);
        float hD = sample(worldX, worldZ - eps, hC);
        float hU = sample(worldX, worldZ + eps, hC);
        glm::vec3 n(hL - hR, 2.0f * eps, hD - hU);
        return glm::normalize(n);
    }
}
