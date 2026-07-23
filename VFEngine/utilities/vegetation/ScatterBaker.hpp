#pragma once

#include "VegetationScatterTypes.hpp"
#include "ScatterRuleEvaluator.hpp"
#include "VegetationTypes.hpp"
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
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
    // localX/localZ are tile-local (world - tileOrigin). `budget` caps how many instances
    // this call may append; on overflow it stops and flags budgetExceeded. A candidate's
    // final jittered position may overhang the tile by < 0.5*spacing; it is still stored
    // in its cell-owner tile (keeps ownership deterministic — the overhang is negligible).
    template <typename HeightFn, typename NormalFn, typename LayerFn>
    ScatterBakeResult bakeScatterForTile(
        const ScatterProfile& profile,
        const std::vector<BillboardPaletteEntry>& palette,
        uint32_t seed,
        float tileOriginX, float tileOriginZ, float worldTileSize,
        HeightFn&& heightFn, NormalFn&& normalFn, LayerFn&& layerFn,
        size_t budget)
    {
        ScatterBakeResult result;

        const float tileMinX = tileOriginX;
        const float tileMinZ = tileOriginZ;
        const float tileMaxX = tileOriginX + worldTileSize;
        const float tileMaxZ = tileOriginZ + worldTileSize;

        for (const auto& rule : profile.rules)
        {
            const float cell = rule.spacing;
            if (cell <= 0.0f)
                continue; // guard: div-by-zero / unbounded grid
            if (rule.paletteEntryIndex >= palette.size())
                continue;
            const BillboardPaletteEntry& entry = palette[rule.paletteEntryIndex];

            const float keepProb = std::clamp(rule.density * profile.globalDensityScale, 0.0f, 1.0f);
            if (keepProb <= 0.0f)
                continue;

            // Cells whose center ((c+0.5)*cell) lies in the half-open span [tileMin, tileMax):
            //   c >= tileMin/cell - 0.5   and   c < tileMax/cell - 0.5
            const int32_t cxLo = static_cast<int32_t>(std::ceil(tileMinX / cell - 0.5f));
            const int32_t cxHi = static_cast<int32_t>(std::ceil(tileMaxX / cell - 0.5f)) - 1;
            const int32_t czLo = static_cast<int32_t>(std::ceil(tileMinZ / cell - 0.5f));
            const int32_t czHi = static_cast<int32_t>(std::ceil(tileMaxZ / cell - 0.5f)) - 1;

            const float jitterScale = std::clamp(rule.positionJitter, 0.0f, 1.0f) * cell;

            for (int32_t cz = czLo; cz <= czHi; ++cz)
            {
                for (int32_t cx = cxLo; cx <= cxHi; ++cx)
                {
                    if (scatter::streamFloat01(cx, cz, seed, scatter::KEEP) >= keepProb)
                        continue;

                    const float centerX = (static_cast<float>(cx) + 0.5f) * cell;
                    const float centerZ = (static_cast<float>(cz) + 0.5f) * cell;
                    const float jx = (scatter::streamFloat01(cx, cz, seed, scatter::JITTER_X) - 0.5f) * jitterScale;
                    const float jz = (scatter::streamFloat01(cx, cz, seed, scatter::JITTER_Z) - 0.5f) * jitterScale;
                    const float worldX = centerX + jx;
                    const float worldZ = centerZ + jz;
                    const float localX = worldX - tileOriginX;
                    const float localZ = worldZ - tileOriginZ;

                    const float height = heightFn(localX, localZ);
                    glm::vec3 normal(0.0f, 1.0f, 0.0f);
                    if (rule.useSlopeMask || rule.alignToNormal)
                        normal = normalFn(localX, localZ);
                    const float layerWeight = rule.useLayerMask ? layerFn(rule.layerIndex, localX, localZ) : 1.0f;

                    const ScatterSample sample{worldX, worldZ, height, normal, layerWeight};
                    if (!ScatterRuleEvaluator::passes(rule, sample))
                        continue;

                    if (result.instances.size() >= budget)
                    {
                        result.budgetExceeded = true;
                        return result;
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
                }
            }
        }

        return result;
    }
}
