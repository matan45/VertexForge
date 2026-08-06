#pragma once

#include <string>

namespace render::gpudriven
{
    // Which arm of the generated terrain composite a pipeline compiles.
    //
    // resources/shaders/material/terrain_material_generated.glsl is #include-d verbatim by BOTH
    // the live terrain fragment shader and the RVT bake shader, and it selects its body from
    // these macros. If the two pipelines are given different flags, baked pages composite
    // differently from the live fallback and the difference shows up as a seam that crawls along
    // the page-residency boundary with the camera — invisible while testing with RVT off.
    //
    // Until VK-1611 that invariant was a comment plus two bools threaded positionally through two
    // call sites. Four flags is where "positional bool list" stops being safe, so it is a type
    // now: TerrainMeshShaderPipeline owns one, TerrainRVTBaker::init takes one, and
    // applyTerrainCompositeMacros below is the single place that turns it into macro definitions.
    // Adding a fifth permutation means adding a field and one line there — it cannot be added to
    // one pipeline and forgotten in the other.
    struct TerrainCompositePermutation
    {
        bool detailMaps = false;      // VK-1610: per-layer normal + emission maps
        bool heightBlend = false;     // VK-1609: height-sharpened splat weights (height in ORM.a)
        bool distanceRescale = false; // VK-1611: second albedo tap at a larger scale
        bool hexTiling = false;       // VK-1612: 3-tap stochastic hex sampling
        // VK-1611: world-anchored macro variation. Cheaper to gate than it looks — the snippet
        // emits it outside the arm chain, so this flag costs one #ifdef instead of doubling the
        // 16 arms, and it saves a measured 4032 bytes of SPIR-V per shader when off.
        bool macroVariation = false;

        friend bool operator==(const TerrainCompositePermutation&,
                               const TerrainCompositePermutation&) = default;
    };

    // Templated on the shader type purely to keep this header free of a core::Shader dependency;
    // there is exactly one call shape and both callers pass core::Shader.
    template <typename ShaderT>
    void applyTerrainCompositeMacros(ShaderT& shader, const TerrainCompositePermutation& perm)
    {
        if (perm.detailMaps)
            shader.addMacroDefinition("TERRAIN_DETAIL_MAPS");
        if (perm.heightBlend)
            shader.addMacroDefinition("TERRAIN_HEIGHT_BLEND");
        if (perm.distanceRescale)
            shader.addMacroDefinition("TERRAIN_DISTANCE_RESCALE");
        if (perm.hexTiling)
            shader.addMacroDefinition("TERRAIN_HEX_TILING");
        if (perm.macroVariation)
            shader.addMacroDefinition("TERRAIN_MACRO_VARIATION");
    }

    // For the one-line log each permutation sync emits.
    inline std::string describeTerrainCompositePermutation(const TerrainCompositePermutation& perm)
    {
        std::string s;
        auto add = [&s](const char* name, bool on) {
            if (!s.empty()) s += ' ';
            s += name;
            s += on ? "=ON" : "=off";
        };
        add("detail", perm.detailMaps);
        add("height", perm.heightBlend);
        add("rescale", perm.distanceRescale);
        add("hex", perm.hexTiling);
        add("macro", perm.macroVariation);
        return s;
    }
}
