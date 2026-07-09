#pragma once

#include <string>
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
        float roughness = 0.9f;
        float metallic = 0.0f;
        float ao = 1.0f;
        float emissionStrength = 0.0f;
        float tilingScale = 1.0f; // always terrain-layer-local, never sourced from a material
    };

    // Resolves a terrain layer's terrain-supported PBR fields from a referenced material's
    // extracted PBR values (VK-1486). Terrain layers source all PBR from a .vfMat/.vfMatInstance;
    // there are no manual per-layer texture/scalar fields. Semantics:
    //   - pbr != nullptr  => copy the material's albedo/normal/ORM texture paths + roughness/
    //                        metallic/ao/emission scalars (terrain's representable subset).
    //   - pbr == nullptr  => no material assigned, or extraction failed: struct defaults
    //                        (empty texture paths + default scalars).
    //   - tilingScale     => always the layer's own value (never sourced from a material).
    ResolvedTerrainLayerPBR resolveTerrainLayerPBR(const terrain::TerrainMaterialLayer& layer,
                                                   const mesh::ExtractedPBRValues* pbr);
}
