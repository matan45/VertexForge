#pragma once

#include <glm/glm.hpp>
#include <cstdint>
#include <string>
#include <vector>

namespace vegetation
{
    // A single procedural scatter rule (VK-1581). One rule drives placement of one
    // billboard palette entry. Placement gates reuse the interactive vegetation brush's
    // mask semantics (VegetationBrushParams) so a baked result matches a hand-painted
    // one; appearance (scale/height/tint/mode) comes from the palette entry, not the rule.
    struct ScatterRule
    {
        uint32_t paletteEntryIndex = 0; // Billboard palette entry this rule places
        float density = 1.0f;           // Per-cell keep probability [0,1] (coverage fraction)
        float spacing = 1.0f;           // Scatter-grid pitch in world units (implicit min-spacing)
        float positionJitter = 0.5f;    // Fraction of a cell the candidate may wander [0,1]
        bool alignToNormal = false;     // Store the terrain normal for align-to-normal rendering

        // Slope mask — cosine of surface normal.y (1=flat, 0=vertical); mirrors VegetationBrushParams
        bool useSlopeMask = false;
        float slopeMinCos = 0.0f;
        float slopeMaxCos = 1.0f;

        // Height mask — world-space Y band
        bool useHeightMask = false;
        float heightMin = 0.0f;
        float heightMax = 100.0f;

        // Noise mask — reject where valueNoise2D(x*freq, z*freq, seed) < threshold (clumping)
        bool useNoiseMask = false;
        float noiseFrequency = 0.1f;
        float noiseThreshold = 0.5f;
        uint32_t noiseSeed = 1337;

        // Layer-weight gate — terrain splat weight of material palette layer `layerIndex`.
        // Place where weight >= layerWeightMin (or, if invertLayer, where weight < it —
        // e.g. avoid a spline-painted road/path layer for free).
        bool useLayerMask = false;
        uint8_t layerIndex = 0;      // Terrain material palette layer [0,31]
        float layerWeightMin = 0.5f; // Min splat weight [0,1] to place
        bool invertLayer = false;    // Flip the gate (exclusion instead of inclusion)

        // Curvature gate (VK-1585) — discrete curvature of the height field, in world-Y
        // units (mean of the 4 axis neighbours minus the centre height). POSITIVE = concave
        // (hollows/valleys collect bushes), NEGATIVE = convex (ridges/peaks stay bare).
        // Sampled in the bake where heightData is directly accessible; it is a gate, not a
        // random draw, so enabling it never perturbs the deterministic instance stream.
        bool useCurvatureMask = false;
        float curvatureMin = -100.0f;
        float curvatureMax = 100.0f;
    };

    // Which palette a scatter profile targets (VK-1585). Metadata recorded in the
    // .vfScatterProfile asset so a saved profile documents its intended domain; the actual
    // binding is which command/palette consumes the profile at bake time.
    enum class ScatterDomain : uint8_t
    {
        Billboard = 0, // grass billboards (rule.paletteEntryIndex -> BillboardPaletteEntry)
        Mesh      = 1, // mesh foliage    (rule.paletteEntryIndex -> FoliageType)
    };

    // A biome (VK-1585) — a named rule set gated by a terrain splat layer, composited by priority
    // with a soft edge-blend. Biomes let one profile paint different vegetation per painted region
    // (e.g. alpine meadow vs forest floor) with smooth transitions instead of hard cuts. The rules
    // inside a biome are the same ScatterRule type as the flat rules; the biome only adds where and
    // how densely they apply. An empty `biomes` list ⇒ byte-for-byte legacy (flat-rules-only) behaviour.
    struct BiomeLayer
    {
        std::string name;                    // editor label (e.g. "Alpine Meadow")
        uint8_t biomeLayerIndex = 0;         // terrain splat/material layer that defines this biome's mask [0,31]
        int32_t priority = 0;                // higher priority soft-suppresses lower biomes in overlaps
        float edgeBlendWidth = 0.15f;        // [0,1] soft-edge band width on the layer weight (0 = hard step)
        float densityScale = 1.0f;           // per-biome density multiplier [0,1]
        std::vector<ScatterRule> rules;      // this biome's placement rules
    };

    // A full scatter profile — the rule set plus global controls. Lives on
    // GrassComponent and serializes in scene JSON beside billboardPalette, and standalone
    // in a reusable .vfScatterProfile asset (VK-1585).
    struct ScatterProfile
    {
        std::vector<ScatterRule> rules;      // biome-agnostic rules (always evaluated)
        std::vector<BiomeLayer> biomes;      // VK-1585 biome-gated rule sets (empty ⇒ legacy behaviour)
        uint32_t globalSeed = 1337;      // Base seed for the deterministic scatter hash
        float globalDensityScale = 1.0f; // Multiplies every rule's density [0,1]
        ScatterDomain domain = ScatterDomain::Billboard; // VK-1585 target-palette hint
    };

    // Per-candidate terrain sample fed to ScatterRuleEvaluator::passes.
    struct ScatterSample
    {
        float worldX = 0.0f;
        float worldZ = 0.0f;
        float height = 0.0f;                 // world-space Y at (worldX, worldZ)
        glm::vec3 normal{0.0f, 1.0f, 0.0f};  // terrain normal (unit)
        float layerWeight = 1.0f;            // splat weight of the rule's layerIndex [0,1]
        float curvature = 0.0f;              // discrete curvature (+ concave / - convex), world-Y units
    };
}
