#include "TerrainLayerPBRResolver.hpp"
#include "../../material/MaterialPBRExtractor.hpp"

namespace render::gpudriven
{
    ResolvedTerrainLayerPBR resolveTerrainLayerPBR(const terrain::TerrainMaterialLayer& layer,
                                                   const mesh::ExtractedPBRValues* pbr)
    {
        ResolvedTerrainLayerPBR out;

        // tilingScale is always terrain-layer-local (never sourced from a material).
        out.tilingScale = layer.tilingScale;

        if (pbr)
        {
            out.albedoPath = pbr->albedoTexturePath;
            out.normalPath = pbr->normalTexturePath;
            out.ormPath = pbr->ormTexturePath;
            out.roughness = pbr->roughness;
            out.metallic = pbr->metallic;
            out.ao = pbr->ao;
            out.emissionStrength = pbr->emission;
        }
        // pbr == nullptr: no material assigned, or extraction failed. Leave the struct defaults
        // (empty texture paths -> shader uses defaults; default roughness/metallic/ao/emission).
        return out;
    }
}
