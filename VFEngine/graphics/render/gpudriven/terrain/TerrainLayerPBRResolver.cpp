#include "TerrainLayerPBRResolver.hpp"
#include "../../material/MaterialPBRExtractor.hpp"
#include "terrain/TerrainAntiTiling.hpp"
#include "terrain/TerrainHeightBlend.hpp"
#include "terrain/TerrainWeatherResponse.hpp"
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

        // VK-1612, the same seam and the same discipline: a layer that did not opt in, or that has
        // no albedo texture to stochastically re-tile (the composite substitutes a constant
        // vec3(0.5) for those), uploads exactly 0.0f and takes the single-tap path — which is the
        // sampling it did before this story. The contrast clamp is a correctness bound as well as
        // a taste one: below 1 the exponent flattens the weights instead of sharpening them,
        // reintroducing the ghosting the ramp exists to remove.
        const bool canHexTile = layer.hexTiling && !out.albedoPath.empty();
        out.hexTilingStrength = canHexTile ? 1.0f : 0.0f;
        out.hexCellScale = canHexTile
            ? std::clamp(layer.hexCellScale, terrain::MIN_HEX_TILING_CELL_SCALE,
                         terrain::MAX_HEX_TILING_CELL_SCALE)
            : 0.0f;
        out.hexContrast = canHexTile
            ? std::clamp(layer.hexContrast, terrain::MIN_HEX_TILING_CONTRAST,
                         terrain::MAX_HEX_TILING_CONTRAST)
            : 0.0f;
        out.hexRotationStrength = canHexTile
            ? std::clamp(layer.hexRotation, 0.0f, terrain::MAX_HEX_TILING_ROTATION)
            : 0.0f;

        // VK-1614, same seam again. Unlike the two above there is no texture precondition: porosity
        // and snow retention modulate the shared weather response, which every layer receives
        // whether or not it resolved to any texture at all. The only gate is the layer's own opt-in.
        //
        // The clamp deliberately has a NON-ZERO lower bound. 0.0f is the sentinel that means "this
        // layer did not opt in", and the shader's authority accumulator relies on it: an unopted
        // layer — and every unused palette slot, which reads a value-initialised
        // TerrainLayerGPUData — must resolve to the neutral default rather than to "absorbs nothing"
        // / "sheds all snow". See TerrainWeatherResponse.hpp.
        out.porosity = terrain::resolveLayerWeatherScalar(layer.porosity, layer.weatherResponse);
        out.snowRetention =
            terrain::resolveLayerWeatherScalar(layer.snowRetention, layer.weatherResponse);

        return out;
    }

    bool terrainMaterialWantsHexTiling(const std::vector<ResolvedTerrainLayerPBR>& layers)
    {
        return std::any_of(layers.begin(), layers.end(),
                           [](const ResolvedTerrainLayerPBR& l) { return l.hexTilingStrength > 0.0f; });
    }

    bool terrainMaterialWantsWeatherResponse(const std::vector<ResolvedTerrainLayerPBR>& layers)
    {
        return std::any_of(layers.begin(), layers.end(),
                           [](const ResolvedTerrainLayerPBR& l)
                           {
                               return l.porosity > 0.0f || l.snowRetention > 0.0f;
                           });
    }

    bool terrainMaterialWantsDetailMaps(const std::vector<ResolvedTerrainLayerPBR>& layers)
    {
        return std::any_of(layers.begin(), layers.end(),
                           [](const ResolvedTerrainLayerPBR& l)
                           {
                               return !l.normalPath.empty() || !l.emissionPath.empty();
                           });
    }

    TerrainAntiTilingGPUData resolveTerrainAntiTiling(const terrain::TerrainAntiTilingSettings& settings)
    {
        TerrainAntiTilingGPUData out{};

        out.macroStrength = std::clamp(settings.macroVariationStrength,
                                       0.0f, terrain::MACRO_VARIATION_MAX_STRENGTH);
        // macroVariationFrequency clamps the size before inverting, so the uploaded frequency is
        // always finite. That matters: the composite's identity-at-strength-0 argument needs
        // finite inputs, exactly like VK-1609's exp2 clamp.
        out.macroFrequency0 = terrain::macroVariationFrequency(settings.macroVariationSize0);
        out.macroFrequency1 = terrain::macroVariationFrequency(settings.macroVariationSize1);
        out.macroSeed = settings.macroVariationSeed;

        out.rescaleStrength = std::clamp(settings.distanceRescaleStrength,
                                         0.0f, terrain::DISTANCE_RESCALE_MAX_STRENGTH);
        out.rescaleScale = std::clamp(settings.distanceRescaleScale,
                                      terrain::DISTANCE_RESCALE_MIN_SCALE,
                                      terrain::DISTANCE_RESCALE_MAX_SCALE);
        out.rescaleKneeLog2 = std::clamp(settings.distanceRescaleKnee,
                                         terrain::DISTANCE_RESCALE_MIN_KNEE,
                                         terrain::DISTANCE_RESCALE_MAX_KNEE);
        out.rescaleWidthLog2 = std::clamp(settings.distanceRescaleWidth,
                                          terrain::DISTANCE_RESCALE_MIN_WIDTH,
                                          terrain::DISTANCE_RESCALE_MAX_WIDTH);

        return out;
    }

    bool terrainMaterialWantsDistanceRescale(const TerrainAntiTilingGPUData& params)
    {
        return params.rescaleStrength > 0.0f
            && params.rescaleScale < terrain::DISTANCE_RESCALE_MAX_SCALE;
    }

    bool terrainMaterialWantsMacroVariation(const TerrainAntiTilingGPUData& params)
    {
        return params.macroStrength > 0.0f;
    }

    TerrainParallaxUBOData resolveTerrainParallax(const terrain::TerrainParallaxSettings& settings)
    {
        TerrainParallaxUBOData out{};

        out.depthMetres = std::clamp(settings.depthMetres, 0.0f, terrain::PARALLAX_MAX_DEPTH);
        out.fadeStart = std::clamp(settings.fadeStart, 0.0f, terrain::PARALLAX_MAX_FADE_DISTANCE);
        // Held strictly past fadeStart. The shader hands both edges straight to smoothstep, and
        // smoothstep(e, e, x) is undefined — the same class of correctness clamp as VK-1611's rescale
        // width, not a matter of taste.
        out.fadeEnd = std::clamp(settings.fadeEnd,
                                 out.fadeStart + terrain::PARALLAX_MIN_FADE_SPAN,
                                 terrain::PARALLAX_MAX_FADE_DISTANCE + terrain::PARALLAX_MIN_FADE_SPAN);
        // parallaxInvReferenceHeight clamps away from zero before inverting, so the uploaded
        // reciprocal is always finite and always >= 1. Both matter: an infinity would poison the
        // clamp in the shader's remap, and a value below 1 would push authored heights off the top
        // plane instead of onto it.
        out.invReferenceHeight = terrain::parallaxInvReferenceHeight(settings.referenceHeight);
        out.steps = std::clamp(settings.steps, terrain::PARALLAX_MIN_STEPS, terrain::PARALLAX_MAX_STEPS);

        return out;
    }

    bool terrainMaterialWantsParallax(const TerrainParallaxUBOData& params)
    {
        // Reads the RESOLVED depth, so the clamp has already been applied and the sentinel is
        // authoritative — the same rule the four gates above follow. Off means TERRAIN_PARALLAX is
        // never defined, so the march, its splat gather and its UBO declaration are not compiled into
        // the shader at all: measured with glslc -O, the feature costs ~6.8-7.0 KB of SPIR-V when on
        // and is byte-identical to the pre-story shader when off.
        return params.depthMetres > terrain::PARALLAX_AUTHORED_EPS;
    }
}
