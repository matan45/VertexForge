#pragma once

#include "VegetationScatterTypes.hpp"
#include "ScatterRuleEvaluator.hpp"
#include "BiomeCompositor.hpp"
#include "VegetationTypes.hpp"
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace vegetation
{
    struct ScatterBakeResult
    {
        std::vector<BillboardInstance> instances; // all tagged InstanceSource::Procedural
        bool budgetExceeded = false;              // true if the budget cap stopped generation
    };

    // Deterministically bake procedural billboard instances for one terrain tile.
    //
    // Cells live on a global lattice of pitch `rule.spacing`; a cell is owned by the tile
    // that contains its CENTER, so every cell is generated exactly once regardless of tile
    // iteration order — the result is seam-free and idempotent. All randomness derives from
    // scatter::hashCell(cx,cz,seed,stream); there is NO stateful RNG. Terrain is read through
    // caller-supplied samplers that close over this tile's arrays (no per-candidate service
    // round-trips):
    //   heightFn(localX, localZ)            -> terrain height (world Y)
    //   normalFn(localX, localZ)            -> terrain normal (unit)
    //   layerFn(layerIndex, localX, localZ) -> splat weight [0,1] of that palette layer
    //   curvatureFn(localX, localZ)         -> discrete curvature (+ concave / - convex), world-Y
    // localX/localZ are tile-local (world - tileOrigin). `budget` caps how many instances
    // this call may append; on overflow it stops and flags budgetExceeded. A candidate's
    // final jittered position may overhang the tile by < 0.5*spacing; it is still stored
    // in its cell-owner tile (keeps ownership deterministic — the overhang is negligible).
    // curvatureFn is only invoked when a rule has useCurvatureMask, so enabling curvature
    // never perturbs the KEEP..WINDPHASE stream order (already-baked scenes stay idempotent).
    template <typename HeightFn, typename NormalFn, typename LayerFn, typename CurvatureFn>
    ScatterBakeResult bakeScatterForTile(
        const ScatterProfile& profile,
        const std::vector<BillboardPaletteEntry>& palette,
        uint32_t seed,
        float tileOriginX, float tileOriginZ, float worldTileSize,
        HeightFn&& heightFn, NormalFn&& normalFn, LayerFn&& layerFn, CurvatureFn&& curvatureFn,
        size_t budget)
    {
        ScatterBakeResult result;

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

        // Sample terrain at an already-jittered candidate, gate through the evaluator, and (on pass)
        // emit a deterministic BillboardInstance. Returns false ONLY when the budget is exhausted
        // (the caller must stop). Shared by the flat and biome loops so the emit logic lives once.
        auto emitCandidate = [&](const ScatterRule& rule, const BillboardPaletteEntry& entry,
                                 int32_t cx, int32_t cz, float worldX, float worldZ) -> bool
        {
            const float localX = worldX - tileOriginX;
            const float localZ = worldZ - tileOriginZ;

            const float height = heightFn(localX, localZ);
            glm::vec3 normal(0.0f, 1.0f, 0.0f);
            if (rule.useSlopeMask || rule.alignToNormal)
                normal = normalFn(localX, localZ);
            const float layerWeight = rule.useLayerMask ? layerFn(rule.layerIndex, localX, localZ) : 1.0f;
            const float curvature = rule.useCurvatureMask ? curvatureFn(localX, localZ) : 0.0f;

            const ScatterSample sample{worldX, worldZ, height, normal, layerWeight, curvature};
            if (!ScatterRuleEvaluator::passes(rule, sample))
                return true; // gated out — keep scanning

            if (result.instances.size() >= budget)
            {
                result.budgetExceeded = true;
                return false;
            }

            // Deterministic equivalents of VegetationBrushServiceImpl::buildInstance,
            // each drawn from its own independent stream instead of a shared unitDist.
            BillboardInstance inst;
            inst.position = glm::vec3(worldX, height, worldZ);
            inst.paletteEntryIndex = rule.paletteEntryIndex;
            inst.rotation = scatter::streamFloat01(cx, cz, seed, scatter::ROTATION) * 6.28318530718f;
            inst.scale = entry.scaleRange.x +
                scatter::streamFloat01(cx, cz, seed, scatter::SCALE) * (entry.scaleRange.y - entry.scaleRange.x);
            inst.heightScale = entry.heightRange.x +
                scatter::streamFloat01(cx, cz, seed, scatter::HEIGHT) * (entry.heightRange.y - entry.heightRange.x);
            inst.tint = 1.0f + (scatter::streamFloat01(cx, cz, seed, scatter::TINT) * 2.0f - 1.0f) * entry.tintJitter;
            inst.windPhase = scatter::streamFloat01(cx, cz, seed, scatter::WINDPHASE);
            inst.normal = rule.alignToNormal ? normal : glm::vec3(0.0f, 1.0f, 0.0f);
            inst.source = InstanceSource::Procedural;
            result.instances.push_back(inst);
            return true;
        };

        // Flat (biome-agnostic) rules — byte-for-byte the VK-1581 behaviour.
        for (const auto& rule : profile.rules)
        {
            const float cell = rule.spacing;
            if (cell <= 0.0f) continue; // guard: div-by-zero / unbounded grid
            if (rule.paletteEntryIndex >= palette.size()) continue;
            const BillboardPaletteEntry& entry = palette[rule.paletteEntryIndex];

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
                    if (!emitCandidate(rule, entry, cx, cz, worldX, worldZ))
                        return result;
                }
        }

        // Biome-layered rules (VK-1585). keepProb is per-cell (splat-membership modulated, with soft
        // higher-priority suppression); jitter is computed BEFORE the KEEP test so membership samples
        // at the candidate's final position. Stream values are position hashes, so this ordering does
        // NOT affect determinism — a placed instance is identical across runs with the same seed.
        for (const auto& biome : profile.biomes)
        {
            for (const auto& rule : biome.rules)
            {
                const float cell = rule.spacing;
                if (cell <= 0.0f) continue;
                if (rule.paletteEntryIndex >= palette.size()) continue;
                const BillboardPaletteEntry& entry = palette[rule.paletteEntryIndex];

                int32_t cxLo, cxHi, czLo, czHi;
                cellRange(cell, cxLo, cxHi, czLo, czHi);
                const float jitterScale = std::clamp(rule.positionJitter, 0.0f, 1.0f) * cell;

                for (int32_t cz = czLo; cz <= czHi; ++cz)
                    for (int32_t cx = cxLo; cx <= cxHi; ++cx)
                    {
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
                        if (scatter::streamFloat01(cx, cz, seed, scatter::KEEP) >= keepProb) continue;

                        if (!emitCandidate(rule, entry, cx, cz, worldX, worldZ))
                            return result;
                    }
            }
        }

        return result;
    }

    // Back-compat overload without a curvature sampler — delegates with a no-op curvature
    // functor (curvature reads 0, so useCurvatureMask rules gate against 0). Existing callers
    // and tests that predate VK-1585 keep compiling unchanged.
    template <typename HeightFn, typename NormalFn, typename LayerFn>
    ScatterBakeResult bakeScatterForTile(
        const ScatterProfile& profile,
        const std::vector<BillboardPaletteEntry>& palette,
        uint32_t seed,
        float tileOriginX, float tileOriginZ, float worldTileSize,
        HeightFn&& heightFn, NormalFn&& normalFn, LayerFn&& layerFn,
        size_t budget)
    {
        return bakeScatterForTile(
            profile, palette, seed, tileOriginX, tileOriginZ, worldTileSize,
            std::forward<HeightFn>(heightFn), std::forward<NormalFn>(normalFn),
            std::forward<LayerFn>(layerFn),
            [](float, float) { return 0.0f; },
            budget);
    }
}
