#pragma once

#include "VegetationScatterTypes.hpp"
#include "../terrain/ValueNoise.hpp"
#include <cstdint>

namespace vegetation
{
    namespace scatter
    {
        // pcg-style integer hash — deterministic and well-distributed. Drives the
        // per-cell random streams so a scatter bake (VK-1581) is idempotent: the same
        // (cell, seed, stream) always yields the same value, independent of any RNG state.
        inline uint32_t pcgHash(uint32_t v)
        {
            uint32_t state = v * 747796405u + 2891336453u;
            uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
            return (word >> 22u) ^ word;
        }

        // Hash a scatter-grid cell (cx,cz) under a global seed and a stream id.
        inline uint32_t hashCell(int32_t cx, int32_t cz, uint32_t seed, uint32_t stream)
        {
            uint32_t h = seed * 0x9E3779B9u;
            h = pcgHash(h ^ static_cast<uint32_t>(cx));
            h = pcgHash(h ^ static_cast<uint32_t>(cz));
            h = pcgHash(h ^ stream);
            return h;
        }

        // Deterministic float in [0,1) for cell (cx,cz) on a given stream.
        inline float streamFloat01(int32_t cx, int32_t cz, uint32_t seed, uint32_t stream)
        {
            return static_cast<float>(hashCell(cx, cz, seed, stream) >> 8) * (1.0f / 16777216.0f);
        }

        // Independent per-cell streams. KEEP decides existence; the rest are the
        // deterministic equivalents of the interactive brush's sequential unitDist draws.
        enum Stream : uint32_t
        {
            KEEP = 0,
            JITTER_X = 1,
            JITTER_Z = 2,
            ROTATION = 3,
            SCALE = 4,
            HEIGHT = 5,
            TINT = 6,
            WINDPHASE = 7,
            // Appended for the procedural MESH scatter baker (VK-1585); append-only so the
            // existing billboard streams keep yielding identical values (idempotency).
            TILT_ANGLE = 8,
            TILT_AZIM = 9,
            SEED = 10
        };
    }

    // Pure placement predicate. The slope/height/noise branches mirror
    // VegetationBrushServiceImpl::passesMasks line-for-line (slope = cosine of normal.y,
    // height = world-Y band, noise = value-noise threshold), so a baked result is
    // consistent with a hand-painted one; the layer-weight gate is the addition the
    // interactive brush does not have. Header-only, zero service/graphics deps.
    struct ScatterRuleEvaluator
    {
        static bool passes(const ScatterRule& r, const ScatterSample& s)
        {
            if (r.useSlopeMask)
            {
                if (s.normal.y < r.slopeMinCos || s.normal.y > r.slopeMaxCos)
                    return false;
            }
            if (r.useHeightMask)
            {
                if (s.height < r.heightMin || s.height > r.heightMax)
                    return false;
            }
            if (r.useNoiseMask)
            {
                float n = terrain::valueNoise2D(s.worldX * r.noiseFrequency,
                                                s.worldZ * r.noiseFrequency, r.noiseSeed);
                if (n < r.noiseThreshold)
                    return false;
            }
            if (r.useLayerMask)
            {
                bool pass = s.layerWeight >= r.layerWeightMin;
                if (r.invertLayer)
                    pass = !pass;
                if (!pass)
                    return false;
            }
            if (r.useCurvatureMask)
            {
                if (s.curvature < r.curvatureMin || s.curvature > r.curvatureMax)
                    return false;
            }
            return true;
        }
    };
}
