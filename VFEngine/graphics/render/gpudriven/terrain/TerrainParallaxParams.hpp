#pragma once

#include <cstdint>

namespace render::gpudriven
{
    // VK-1625 — the GPU side of terrain POM-lite. Mirrors TerrainParallaxUBO in mesh_terrain.glsl,
    // bound at set 11, binding 7.
    //
    // Set 11 is the right home for the same reason VK-1614's weather params are: it does NOT exist in
    // the RVT bake pipeline (TerrainRVTBaker binds {weightMap, bindless, terrainData} and its shader
    // declares only binding 0 of the last one), so a view-dependent parameter cannot reach a
    // view-independent bake even by accident. It is also the only place left: the terrain draw's push
    // constants are already 144 bytes, past Vulkan's 128-byte guaranteed minimum, so the story's
    // "no push-constant growth" is a hard constraint rather than a preference.
    //
    // Unlike the weather block this needs no sampler — parallax reads the height it marches from the
    // layer ORM textures the composite already binds — so it takes one binding, not two.
    //
    // 32 bytes with named padding, following VK-1614's CausticParams: a follow-up scalar then costs an
    // assignment rather than a descriptor change. std140-safe as written — every member is a 4-byte
    // scalar, so none introduces a larger base alignment, and the total is already a multiple of 16.
    struct TerrainParallaxUBOData
    {
        // 0 is the off sentinel, exactly as VK-1611's macroVariationStrength is. It is also what the
        // eager dummy write leaves here, which is what makes an unassigned binding harmless: the
        // shader's `if (tpDepth > 0.0)` never opens.
        float depthMetres = 0.0f;
        float fadeStart = 0.0f;
        float fadeEnd = 0.0f;
        // 1 / referenceHeight. Inverted on upload so the shader multiplies rather than divides per
        // fragment per march step — the rule macroVariationFrequency() already follows. Exactly 1.0f
        // at the authored default, where the shader's remap is the bitwise identity.
        float invReferenceHeight = 1.0f;
        // Uniform loop bound, so the march stays in uniform control flow. Clamped into
        // [PARALLAX_MIN_STEPS, PARALLAX_MAX_STEPS] by resolveTerrainParallax.
        uint32_t steps = 1u;
        uint32_t reservedFlags = 0u;
        float reserved0 = 0.0f;
        float reserved1 = 0.0f;
    };

    static_assert(sizeof(TerrainParallaxUBOData) == 32,
                  "mirrored by TerrainParallaxUBO in mesh_terrain.glsl");
}
