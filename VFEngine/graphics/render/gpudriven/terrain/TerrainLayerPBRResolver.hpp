#pragma once

#include <string>
#include <vector>
#include "terrain/TerrainMaterialTypes.hpp"

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
}
