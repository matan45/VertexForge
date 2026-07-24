#pragma once

// VK-1585 — deterministic procedural MESH scatter into the packed FoliageInstance store.
//
// This is the mesh-foliage sibling of vegetation::bakeScatterForTile: it reuses the SAME rule
// taxonomy (vegetation::ScatterProfile / ScatterRule / ScatterRuleEvaluator) and the SAME
// per-cell hash streams (vegetation::scatter::hashCell) so a bake is deterministic + seam-free
// by construction (same seed -> identical instance set), and curvature/slope/height/noise/layer
// gates behave identically to the grass scatter. It differs only in the EMIT target: it produces
// foliage::FoliageInstance and reads appearance from the FoliageType palette (mesh, not billboard).
//
// Placement GATES come from the ScatterRule (the "same rule evaluator" the ticket asks for);
// APPEARANCE (rotation/scale/tilt/align-to-normal) comes from the FoliageType. A FoliageType's
// own brush-time slope/altitude masks are intentionally NOT re-applied here — the ScatterRule
// gates are authoritative on the procedural path.
//
// Every emitted instance is tagged FoliageInstanceFlags::Procedural so a Regenerate can replace
// only procedural instances and preserve hand-painted foliage.

#include "../vegetation/VegetationScatterTypes.hpp"
#include "../vegetation/ScatterRuleEvaluator.hpp"
#include "../vegetation/BiomeCompositor.hpp"
#include "../vegetation/ScatterLoop.hpp"
#include "FoliageTypes.hpp"
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace foliage
{
    struct FoliageScatterBakeResult
    {
        std::vector<FoliageInstance> instances; // all tagged FoliageInstanceFlags::Procedural
        bool budgetExceeded = false;
    };

    // Deterministically bake procedural foliage instances for one terrain tile. Contract matches
    // vegetation::bakeScatterForTile (cell lattice of pitch rule.spacing owned by the tile that
    // contains a cell's centre; caller-supplied tile-local samplers). curvatureFn is only invoked
    // for rules with useCurvatureMask, so it never perturbs the deterministic stream order.
    template <typename HeightFn, typename NormalFn, typename LayerFn, typename CurvatureFn>
    FoliageScatterBakeResult bakeFoliageScatterForTile(
        const vegetation::ScatterProfile& profile,
        const std::vector<FoliageType>& palette,
        uint32_t seed,
        float tileOriginX, float tileOriginZ, float worldTileSize,
        HeightFn&& heightFn, NormalFn&& normalFn, LayerFn&& layerFn, CurvatureFn&& curvatureFn,
        size_t budget)
    {
        namespace scatter = vegetation::scatter;
        constexpr float kTwoPi = 6.28318530718f;
        constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;

        FoliageScatterBakeResult result;

        // Sample terrain at an already-jittered candidate, gate through the SHARED evaluator, and
        // (on pass) emit a deterministic FoliageInstance tagged Procedural. Returns false only when
        // the budget is exhausted. Shared by the flat and biome loops.
        auto emitCandidate = [&](const vegetation::ScatterRule& rule, const FoliageType& type,
                                 int32_t cx, int32_t cz, float worldX, float worldZ) -> bool
        {
            const float localX = worldX - tileOriginX;
            const float localZ = worldZ - tileOriginZ;

            const float height = heightFn(localX, localZ);
            glm::vec3 normal(0.0f, 1.0f, 0.0f);
            if (rule.useSlopeMask || type.alignToNormal)
                normal = normalFn(localX, localZ);
            const float layerWeight = rule.useLayerMask ? layerFn(rule.layerIndex, localX, localZ) : 1.0f;
            const float curvature = rule.useCurvatureMask ? curvatureFn(localX, localZ) : 0.0f;

            const vegetation::ScatterSample sample{worldX, worldZ, height, normal, layerWeight, curvature};
            if (!vegetation::ScatterRuleEvaluator::passes(rule, sample))
                return true;

            if (result.instances.size() >= budget)
            {
                result.budgetExceeded = true;
                return false;
            }

            FoliageInstance fi;
            fi.position = glm::vec3(worldX, height, worldZ);
            fi.typeIndex = static_cast<uint16_t>(rule.paletteEntryIndex);

            // Rotation over the type's Y-range (degrees -> radians).
            const float rotDeg = type.rotationYRange.x +
                scatter::streamFloat01(cx, cz, seed, scatter::ROTATION) *
                (type.rotationYRange.y - type.rotationYRange.x);
            fi.rotationY = rotDeg * kDegToRad;

            // Uniform scale over scaleRange, extra Y multiplier over heightRange.
            const float s = type.scaleRange.x +
                scatter::streamFloat01(cx, cz, seed, scatter::SCALE) * (type.scaleRange.y - type.scaleRange.x);
            const float hMul = type.heightRange.x +
                scatter::streamFloat01(cx, cz, seed, scatter::HEIGHT) * (type.heightRange.y - type.heightRange.x);
            fi.scale = glm::vec3(s, s * hMul, s);

            // Orientation: align to the (optionally random-tilted) surface normal.
            glm::vec3 outNormal(0.0f, 1.0f, 0.0f);
            uint16_t flags = FoliageInstanceFlags::Procedural;
            if (type.alignToNormal)
            {
                outNormal = normal;
                if (type.randomTilt > 0.0f)
                {
                    const float tiltRad = glm::radians(type.randomTilt) *
                        scatter::streamFloat01(cx, cz, seed, scatter::TILT_ANGLE);
                    const float azim = scatter::streamFloat01(cx, cz, seed, scatter::TILT_AZIM) * kTwoPi;
                    const glm::vec3 h(std::cos(azim), 0.0f, std::sin(azim)); // horizontal unit dir
                    outNormal = glm::normalize(outNormal * std::cos(tiltRad) + h * std::sin(tiltRad));
                    flags |= FoliageInstanceFlags::Tilt;
                }
            }
            fi.normal = outNormal;

            fi.windPhase = scatter::streamFloat01(cx, cz, seed, scatter::WINDPHASE);
            fi.seed = scatter::hashCell(cx, cz, seed, scatter::SEED); // deterministic per-instance
            fi.tint = 0xFFFFFFFFu;                                    // no per-instance override

            if (type.collision)     flags |= FoliageInstanceFlags::Collider;
            if (type.navContribute) flags |= FoliageInstanceFlags::NavContribute;
            fi.flags = flags;

            result.instances.push_back(fi);
            return true;
        };

        // Flat + biome cell-scan is shared with the billboard baker (one copy of the ownership math,
        // gating, stream order, and the #10 biome KEEP early-out) — see ../vegetation/ScatterLoop.hpp.
        vegetation::detail::bakeScatterLoop(profile, palette, seed, tileOriginX, tileOriginZ,
                                            worldTileSize, layerFn, emitCandidate);
        return result;
    }
}
