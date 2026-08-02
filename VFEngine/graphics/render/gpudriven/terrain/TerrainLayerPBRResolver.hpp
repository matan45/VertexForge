#pragma once

#include <string>
#include <vector>
#include "terrain/TerrainMaterialTypes.hpp"
#include "../GPUDrivenTypes.hpp"
#include "TerrainParallaxParams.hpp"

namespace render::mesh
{
    struct ExtractedPBRValues;
}

namespace render::gpudriven
{
    // Terrain-supported PBR fields resolved for a single terrain layer, ready to be
    // flattened into TerrainLayerGPUData. Texture members are resolved file paths
    // (empty => no texture / default sentinel), consumed by the bindless registration path.
    struct ResolvedTerrainLayerPBR
    {
        std::string albedoPath;
        std::string normalPath;
        std::string ormPath;
        std::string emissionPath;
        float roughness = 0.9f;
        float metallic = 0.0f;
        float ao = 1.0f;
        float emissionStrength = 0.0f;
        float tilingScale = 1.0f; // always terrain-layer-local, never sourced from a material
        // VK-1609: height-blend contrast uploaded to TerrainLayerGPUData. 0 => this layer blends
        // linearly, bit-identically to the pre-VK-1609 composite.
        float heightBlendContrast = 0.0f;
        // VK-1612: hex stochastic sampling. 0 strength => single-tap, i.e. the sampling this layer
        // did before the story existed.
        float hexTilingStrength = 0.0f;
        float hexCellScale = 0.0f;
        float hexContrast = 0.0f;
        float hexRotationStrength = 0.0f;
        // VK-1614: per-layer weather response. 0 => this layer did not opt in, and the shader falls
        // back to the derived porosity / full snow retention it used before the story existed. The
        // sentinel must be exactly 0 — see TerrainWeatherResponse.hpp for why.
        float porosity = 0.0f;
        float snowRetention = 0.0f;
    };

    // Resolves a terrain layer's terrain-supported PBR fields from a referenced material's
    // extracted PBR values (VK-1486). Terrain layers source all PBR from a .vfMat/.vfMatInstance;
    // there are no manual per-layer texture/scalar fields. Semantics:
    //   - pbr != nullptr  => copy the material's albedo/normal/ORM/emission texture paths +
    //                        roughness/metallic/ao/emission scalars (terrain's representable subset).
    //   - pbr == nullptr  => no material assigned, or extraction failed: struct defaults
    //                        (empty texture paths + default scalars).
    //   - tilingScale     => always the layer's own value (never sourced from a material).
    //   - heightBlendContrast (VK-1609) => the layer's own heightContrast, clamped to
    //                        [0, terrain::MAX_HEIGHT_BLEND_CONTRAST], but ONLY when the layer
    //                        selects HeightBlend and the resolved material actually has a packed
    //                        ORM (height rides ORM alpha). Otherwise exactly 0.0f, which is what
    //                        makes such a layer bit-identical to the pre-VK-1609 linear composite.
    //   hexTiling* (VK-1612) => the layer's own hex settings, clamped, but ONLY when the layer opts
    //                        in AND resolved to an albedo texture. A layer with no albedo composites
    //                        a constant vec3(0.5); hex-sampling a constant is pure waste, so it is
    //                        forced to strength 0 and takes the single-tap path.
    //   porosity / snowRetention (VK-1614) => the layer's own values, clamped into
    //                        [terrain::MIN_LAYER_WEATHER_SCALAR, 1], but ONLY when the layer opts in.
    //                        Otherwise exactly 0.0f, the sentinel meaning "fall back to the derived
    //                        porosity / full retention". Unlike the two above there is no texture
    //                        precondition: the weather response applies to every layer.
    ResolvedTerrainLayerPBR resolveTerrainLayerPBR(const terrain::TerrainMaterialLayer& layer,
                                                   const mesh::ExtractedPBRValues* pbr);

    // VK-1610 - does this terrain material actually carry per-layer detail maps?
    //
    // This is what drives the TERRAIN_DETAIL_MAPS permutation, so that "detail maps on" costs
    // nothing at all on terrain nobody authored normal or emission maps for. The live composite
    // is already content-proportional (a layer with normalTextureIndex == 0 takes a branch, not
    // a fetch), but the RVT pool is NOT: switching to the four-plane layout costs 20 bytes per
    // texel instead of 8 whether or not a single layer uses it, which at the default budget cuts
    // resident pages from 1024 to 400. Deriving the permutation from the material is what keeps
    // that bill off terrain that would get nothing for it.
    //
    // Deliberately reads the RESOLVED paths, not the authored layer: resolveTerrainLayerPBR is
    // where a material reference becomes a texture path, so a layer whose .vfMat failed to load
    // correctly reads as "no detail maps" rather than promising maps that will never bind.
    [[nodiscard]] bool terrainMaterialWantsDetailMaps(const std::vector<ResolvedTerrainLayerPBR>& layers);

    // VK-1611 - the material-global counterpart of resolveTerrainLayerPBR: the one seam where
    // authored anti-tiling intent becomes GPU scalars. Everything is clamped here rather than at
    // the use site, because two of the clamps are correctness invariants and not taste:
    //   * a macro-variation SIZE at or below zero would upload an infinite frequency;
    //   * a rescale WIDTH of zero would reach smoothstep with equal edges, which is undefined.
    // Sizes are authored in world metres and inverted here, so the shader multiplies instead of
    // dividing per fragment.
    [[nodiscard]] TerrainAntiTilingGPUData resolveTerrainAntiTiling(
        const terrain::TerrainAntiTilingSettings& settings);

    // Does this material actually want the second, larger-scale albedo tap? Drives the
    // TERRAIN_DISTANCE_RESCALE permutation, so "off" means the extra textureGrad is not compiled
    // into the shader at all rather than being multiplied by zero.
    //
    // A rescaleScale of exactly 1.0 counts as OFF: the far tap would sample the same texels as the
    // near one, so the material would pay a full extra fetch per layer to blend a value with
    // itself. That is the one configuration where the feature is provably pure cost.
    [[nodiscard]] bool terrainMaterialWantsDistanceRescale(const TerrainAntiTilingGPUData& params);

    // Does this material actually want macro variation? Drives TERRAIN_MACRO_VARIATION. At
    // strength 0 the shader's multiplier is exactly 1.0 either way, so this is a pure cost gate,
    // not a correctness one — but it is worth 4032 bytes of SPIR-V (~200 ALU) per terrain shader.
    [[nodiscard]] bool terrainMaterialWantsMacroVariation(const TerrainAntiTilingGPUData& params);

    // VK-1612 - does any layer actually hex-tile? Drives TERRAIN_HEX_TILING, so a material with no
    // opted-in layer compiles the shader it would have compiled before the story existed. Reads
    // the RESOLVED strength, so a layer that opted in but whose material failed to supply an albedo
    // texture correctly reads as "no".
    [[nodiscard]] bool terrainMaterialWantsHexTiling(const std::vector<ResolvedTerrainLayerPBR>& layers);

    // VK-1614 - does any layer actually author a weather response? Drives TERRAIN_WEATHER_RESPONSE,
    // so a material with no opted-in layer compiles the shader it would have compiled before the
    // story existed — and, more to the point, does not pay the per-fragment 8-channel splat gather
    // the response needs. That gather is the one real cost here (~150 ALU, comparable to the 4032
    // bytes of SPIR-V VK-1611 gated macro variation for), and it cannot be avoided in the
    // RVT-resolved path any other way: the composite never runs there, so per-layer identity is gone
    // by the time wetness/snow are applied.
    //
    // Reads the RESOLVED scalars, so the opt-in and the clamp are already applied and the sentinel
    // is authoritative.
    [[nodiscard]] bool terrainMaterialWantsWeatherResponse(const std::vector<ResolvedTerrainLayerPBR>& layers);

    // VK-1625 - the material-global counterpart for POM-lite, and the one seam where authored parallax
    // intent becomes GPU scalars. Every clamp lives here rather than at the use site, because two of
    // them are correctness invariants and not taste:
    //   * fadeEnd is forced strictly past fadeStart, since the shader feeds both to smoothstep;
    //   * referenceHeight is clamped away from zero before being INVERTED, since the shader gets the
    //     reciprocal and an infinity would poison the clamp its height remap relies on.
    [[nodiscard]] TerrainParallaxUBOData resolveTerrainParallax(const terrain::TerrainParallaxSettings& settings);

    // Does this material actually want parallax? Drives TERRAIN_PARALLAX, which is a LIVE-ONLY macro:
    // it must never join TerrainCompositePermutation, because that type is shared with TerrainRVTBaker
    // and a view-dependent offset in a camera-less, top-down bake is undefined. Flipping it therefore
    // rides GPUDrivenRendererTerrain's `liveOnlyChanged` path, which recompiles the terrain pipeline
    // without tearing down the baker or invalidating a single resident page.
    [[nodiscard]] bool terrainMaterialWantsParallax(const TerrainParallaxUBOData& params);
}
