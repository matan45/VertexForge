#include "TerrainLayerPBRResolver.hpp"
#include "../../material/MaterialPBRExtractor.hpp"
#include "terrain/TerrainHeightBlend.hpp"
#include <algorithm>

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
            out.emissionPath = pbr->emissionTexturePath;
            out.roughness = pbr->roughness;
            out.metallic = pbr->metallic;
            out.ao = pbr->ao;
            out.emissionStrength = pbr->emission;
        }
        // pbr == nullptr: no material assigned, or extraction failed. Leave the struct defaults
        // (empty texture paths -> shader uses defaults; default roughness/metallic/ao/emission).

        // VK-1609. This is the one seam where authoring intent becomes a GPU scalar, so it is
        // where the "layers without a height map fall back to linear, bit-identically" guarantee
        // is enforced — per layer, not per material. Height is packed in the ORM texture's alpha
        // channel, so a layer with no ORM has no height source at all and must upload exactly
        // 0.0f: the composite's mix() then returns exactly 1.0 and the layer's weight is
        // untouched down to the last bit. The clamp is a correctness invariant (see
        // MAX_HEIGHT_BLEND_CONTRAST), not just a sanity range.
        const bool hasHeightSource = !out.ormPath.empty();
        out.heightBlendContrast =
            (layer.blendMode == terrain::TerrainLayerBlendMode::HeightBlend && hasHeightSource)
                ? std::clamp(layer.heightContrast, 0.0f, terrain::MAX_HEIGHT_BLEND_CONTRAST)
                : 0.0f;

        return out;
    }

    bool terrainMaterialWantsDetailMaps(const std::vector<ResolvedTerrainLayerPBR>& layers)
    {
        return std::any_of(layers.begin(), layers.end(),
                           [](const ResolvedTerrainLayerPBR& l)
                           {
                               return !l.normalPath.empty() || !l.emissionPath.empty();
                           });
    }
}
