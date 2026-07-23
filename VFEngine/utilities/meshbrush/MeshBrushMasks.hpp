#pragma once

#include <glm/glm.hpp>
#include "MeshBrushTypes.hpp"
#include "../terrain/ValueNoise.hpp"

namespace meshbrush
{
    // Pure predicate mirroring VegetationBrushServiceImpl::passesMasks: does a candidate at
    // (candX, candZ) with world height candY and terrain surface normal `normal` pass the
    // brush's active slope/height/noise masks? Header-only + inline so it is unit-testable
    // without the EventDispatcher or the terrain service.
    inline bool passesMasks(const MeshBrushParams& p, float candY,
                            const glm::vec3& normal, float candX, float candZ)
    {
        if (p.useSlopeMask)
        {
            if (normal.y < p.slopeMinCos || normal.y > p.slopeMaxCos)
                return false;
        }
        if (p.useHeightMask)
        {
            if (candY < p.heightMin || candY > p.heightMax)
                return false;
        }
        if (p.useNoiseMask)
        {
            float n = terrain::valueNoise2D(candX * p.noiseFrequency,
                                            candZ * p.noiseFrequency, p.noiseSeed);
            if (n < p.noiseThreshold)
                return false;
        }
        return true;
    }
}
