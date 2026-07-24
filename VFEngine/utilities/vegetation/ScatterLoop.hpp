#pragma once

// Shared deterministic cell-scan for the scatter bakers (billboard + mesh foliage).
//
// Both vegetation::bakeScatterForTile and foliage::bakeFoliageScatterForTile walk the SAME flat +
// biome rule loops over the tile's owned cell lattice, drawing the SAME per-cell KEEP/JITTER hash
// streams. Only the EMIT body differs (which instance type, which appearance streams) and the
// normal-sampling decision — both of which live entirely inside the caller's `emitFn`. Factoring
// the loop here keeps grass and mesh scatter byte-for-byte identical by construction (one copy of
// the ownership math, gating, and stream order).
//
// Determinism note: scatter streams are STATELESS position hashes (scatter::hashCell), so the KEEP
// draw's value does not depend on when it is drawn. That is what makes the code-review #10 early-out
// exact: in the biome loop we draw KEEP first and reject cells whose draw exceeds a conservative
// upper bound on keepProb BEFORE paying for the splat sample + higher-priority membership loop. A
// cell rejected by that gate would also fail the exact test, and cells that survive run the full,
// unchanged path — so the emitted set and every emitted value are identical to the pre-#10 code.

#include "VegetationScatterTypes.hpp"
#include "ScatterRuleEvaluator.hpp"
#include "BiomeCompositor.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace vegetation::detail
{
    // Drive emitFn(rule, palette[rule.paletteEntryIndex], cx, cz, worldX, worldZ) for every surviving
    // candidate cell, flat rules first then biome-layered rules. emitFn returns false to stop early
    // (budget exhausted). layerFn(layerIndex, localX, localZ) supplies the splat weight for biome
    // membership; the per-emit terrain samplers (height/normal/curvature) are captured by emitFn.
    template <typename PaletteVec, typename LayerFn, typename EmitFn>
    void bakeScatterLoop(
        const ScatterProfile& profile,
        const PaletteVec& palette,
        uint32_t seed,
        float tileOriginX, float tileOriginZ, float worldTileSize,
        LayerFn&& layerFn,
        EmitFn&& emitFn)
    {
        const float tileMinX = tileOriginX;
        const float tileMinZ = tileOriginZ;
        const float tileMaxX = tileOriginX + worldTileSize;
        const float tileMaxZ = tileOriginZ + worldTileSize;

        // Cell index range whose CENTRES ((c+0.5)*cell) fall in the half-open span [tileMin,tileMax)
        // — the seam-free ownership rule (a cell belongs to the tile containing its centre).
        auto cellRange = [&](float cell, int32_t& cxLo, int32_t& cxHi, int32_t& czLo, int32_t& czHi)
        {
            cxLo = static_cast<int32_t>(std::ceil(tileMinX / cell - 0.5f));
            cxHi = static_cast<int32_t>(std::ceil(tileMaxX / cell - 0.5f)) - 1;
            czLo = static_cast<int32_t>(std::ceil(tileMinZ / cell - 0.5f));
            czHi = static_cast<int32_t>(std::ceil(tileMaxZ / cell - 0.5f)) - 1;
        };

        // Flat (biome-agnostic) rules — KEEP tested first (already optimal, unchanged from VK-1581).
        for (const auto& rule : profile.rules)
        {
            const float cell = rule.spacing;
            if (cell <= 0.0f) continue; // guard: div-by-zero / unbounded grid
            if (rule.paletteEntryIndex >= palette.size()) continue;

            const float keepProb = std::clamp(rule.density * profile.globalDensityScale, 0.0f, 1.0f);
            if (keepProb <= 0.0f) continue;

            int32_t cxLo, cxHi, czLo, czHi;
            cellRange(cell, cxLo, cxHi, czLo, czHi);
            const float jitterScale = std::clamp(rule.positionJitter, 0.0f, 1.0f) * cell;

            for (int32_t cz = czLo; cz <= czHi; ++cz)
                for (int32_t cx = cxLo; cx <= cxHi; ++cx)
                {
                    if (scatter::streamFloat01(cx, cz, seed, scatter::KEEP) >= keepProb)
                        continue;
                    const float centerX = (static_cast<float>(cx) + 0.5f) * cell;
                    const float centerZ = (static_cast<float>(cz) + 0.5f) * cell;
                    const float worldX = centerX + (scatter::streamFloat01(cx, cz, seed, scatter::JITTER_X) - 0.5f) * jitterScale;
                    const float worldZ = centerZ + (scatter::streamFloat01(cx, cz, seed, scatter::JITTER_Z) - 0.5f) * jitterScale;
                    if (!emitFn(rule, palette[rule.paletteEntryIndex], cx, cz, worldX, worldZ))
                        return;
                }
        }

        // Biome-layered rules (VK-1585). keepProb is per-cell (splat-membership modulated, with soft
        // higher-priority suppression); jitter is computed BEFORE membership so it samples at the
        // candidate's final position. Position-hash streams make the ordering determinism-neutral.
        for (const auto& biome : profile.biomes)
        {
            for (const auto& rule : biome.rules)
            {
                const float cell = rule.spacing;
                if (cell <= 0.0f) continue;
                if (rule.paletteEntryIndex >= palette.size()) continue;

                int32_t cxLo, cxHi, czLo, czHi;
                cellRange(cell, cxLo, cxHi, czLo, czHi);
                const float jitterScale = std::clamp(rule.positionJitter, 0.0f, 1.0f) * cell;

                // code-review #10: conservative upper bound on the exact per-cell keepProb. Both the
                // membership m and (1 - mHi) lie in [0,1] and clamp is monotone, so
                //   keepProb = clamp(density*biomeScale*globalScale * m * (1-mHi))
                //           <= clamp(density*biomeScale*globalScale) = keepUpper.
                // A cell whose KEEP draw is >= keepUpper would also fail the exact test below, so we
                // reject it before the (expensive) splat sample + higher-priority membership loop.
                const float keepUpper = std::clamp(
                    rule.density * biome.densityScale * profile.globalDensityScale, 0.0f, 1.0f);

                for (int32_t cz = czLo; cz <= czHi; ++cz)
                    for (int32_t cx = cxLo; cx <= cxHi; ++cx)
                    {
                        const float keepDraw = scatter::streamFloat01(cx, cz, seed, scatter::KEEP);
                        if (keepDraw >= keepUpper) continue; // cheap conservative reject (#10)

                        const float centerX = (static_cast<float>(cx) + 0.5f) * cell;
                        const float centerZ = (static_cast<float>(cz) + 0.5f) * cell;
                        const float worldX = centerX + (scatter::streamFloat01(cx, cz, seed, scatter::JITTER_X) - 0.5f) * jitterScale;
                        const float worldZ = centerZ + (scatter::streamFloat01(cx, cz, seed, scatter::JITTER_Z) - 0.5f) * jitterScale;
                        const float localX = worldX - tileOriginX;
                        const float localZ = worldZ - tileOriginZ;

                        const float m = biomeMembership(layerFn(biome.biomeLayerIndex, localX, localZ), biome.edgeBlendWidth);
                        float mHi = 0.0f;
                        for (const auto& other : profile.biomes)
                            if (other.priority > biome.priority)
                                mHi = std::max(mHi, biomeMembership(layerFn(other.biomeLayerIndex, localX, localZ), other.edgeBlendWidth));

                        const float keepProb = biomeEffectiveKeepProb(rule.density, biome.densityScale,
                                                                      profile.globalDensityScale, m, mHi);
                        if (keepProb <= 0.0f) continue;
                        if (keepDraw >= keepProb) continue; // exact test, reusing the same KEEP draw

                        if (!emitFn(rule, palette[rule.paletteEntryIndex], cx, cz, worldX, worldZ))
                            return;
                    }
            }
        }
    }
}
