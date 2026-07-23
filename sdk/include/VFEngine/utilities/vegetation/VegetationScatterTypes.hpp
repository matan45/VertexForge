#pragma once

#include <glm/glm.hpp>
#include <cstdint>
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
    };

    // A full scatter profile — the rule set plus global controls. Lives on
    // GrassComponent and serializes in scene JSON beside billboardPalette.
    struct ScatterProfile
    {
        std::vector<ScatterRule> rules;
        uint32_t globalSeed = 1337;      // Base seed for the deterministic scatter hash
        float globalDensityScale = 1.0f; // Multiplies every rule's density [0,1]
    };

    // Per-candidate terrain sample fed to ScatterRuleEvaluator::passes.
    struct ScatterSample
    {
        float worldX = 0.0f;
        float worldZ = 0.0f;
        float height = 0.0f;                 // world-space Y at (worldX, worldZ)
        glm::vec3 normal{0.0f, 1.0f, 0.0f};  // terrain normal (unit)
        float layerWeight = 1.0f;            // splat weight of the rule's layerIndex [0,1]
    };
}
