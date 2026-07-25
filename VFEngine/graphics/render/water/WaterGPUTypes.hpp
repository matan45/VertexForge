#pragma once

#include "../../../utilities/water/WaterBodyMath.hpp"
#include "../../../utilities/water/WaterTileGrid.hpp"
#include <glm/glm.hpp>
#include <cstdint>

namespace render::water
{
    constexpr uint32_t WATER_LOD_COUNT = ::water::WATER_TILE_LOD_COUNT;
    constexpr uint32_t WATER_LOD0_SUBDIVISIONS = 64;
    constexpr uint32_t WATER_LOD1_SUBDIVISIONS = 32;
    constexpr uint32_t WATER_LOD2_SUBDIVISIONS = 16;
    constexpr uint32_t WATER_LOD3_SUBDIVISIONS = 8;

    // Per-tile instance data — canonical definition in ::water::WaterTileGPUData
    using WaterTileGPUData = ::water::WaterTileGPUData;

    // Push constants for ocean rendering (must fit 128-byte limit)
    struct WaterPushConstants
    {
        glm::vec4 shallowColor;         // 16 bytes
        glm::vec4 deepColor;            // 16 bytes
        float maxVisibleDepth;          // 4
        float fresnelPower;             // 4
        float oceanChoppiness;          // 4
        float oceanPatchSize0;          // 4  (swell band patch size)
        float oceanFoamThreshold;       // 4
        float refractionStrength;       // 4
        float refractionChromatic;      // 4
        float refractionDepthScale;     // 4
        float oceanPatchSize1;          // 4  (agitation band patch size)
        float oceanPatchSize2;          // 4  (ripples band patch size)
        uint32_t bandEnableMask;        // 4  (bit 0=swell, bit 1=agitation, bit 2=ripples)
        float shoreFoamRange;           // 4  (world units: how far from shore foam extends)
        float shoreFoamIntensity;       // 4
        float shoreBreakingStrength;    // 4
        // VK-1604: per-VIEW mask ANDed with WaterExtendedParams::flags in the shader.
        // WaterExtendedParams is filled once per frame in updateWater, but renderWaterDraw runs
        // once for the main view plus once per RTT / reflection-probe view, so anything that must
        // differ between views cannot live in the UBO. RTT views clear the SSR and absorption bits
        // (their set 9 points at the main view's color copy and depth).
        uint32_t viewFlagMask;          // 4
    };
    static_assert(sizeof(WaterPushConstants) == 92);

    // VK-1604: feature flags shared by WaterExtendedParams::flags and WaterPushConstants::viewFlagMask.
    // MUST stay in sync with resources/shaders/water/water_params.glsl — there is no codegen.
    constexpr uint32_t WATER_FLAG_SSR = 1u << 0;
    constexpr uint32_t WATER_FLAG_ABSORPTION = 1u << 1;
    constexpr uint32_t WATER_FLAG_HEX = 1u << 2;
    constexpr uint32_t WATER_FLAG_SSR_DEBUG = 1u << 3;
    // VK-1605
    constexpr uint32_t WATER_FLAG_SHOALING = 1u << 4;
    constexpr uint32_t WATER_FLAG_SHORE_WAVES = 1u << 5;
    // The shore-depth field has completed at least one bake. Gates the fragment stage's use of the
    // interpolated true water depth; until then the screen-space reconstruction is all there is.
    constexpr uint32_t WATER_FLAG_SHORE_FIELD = 1u << 6;
    // VK-1606: the interactive ripple patch (set 9 binding 4) has valid contents. Gates both the
    // vertex displacement and the fragment foam contribution.
    constexpr uint32_t WATER_FLAG_RIPPLES = 1u << 7;
    // VK-1607: at least one water body is present, so ocean tiles must discard the fragments that
    // fall inside bodyClipRects[0..bodyClipCount). Body tiles exempt themselves via their own
    // per-tile is-body bit, so this flag is safe to leave set for every tile in the draw.
    constexpr uint32_t WATER_FLAG_BODY_CLIP = 1u << 8;
    // bits 9..15 still free

    // Flags an RTT / reflection-probe view is allowed to keep. SSR is view-dependent (baking it
    // into a probe cubemap would be wrong from every direction but the capture one) and both SSR
    // and absorption read set 9, which belongs to the main view.
    //
    // VK-1605: SHOALING and SHORE_WAVES are deliberately NOT cleared. Like HEX they change VERTEX
    // displacement, and a probe whose water sits at a different height than the main view's would
    // reflect a surface that does not exist. The shore-depth texture is therefore also written into
    // the refraction dummy set (WaterPipeline::createRefractionDummy) which is what RTT views bind.
    //
    // VK-1606: RIPPLES follows the same rule for the same reason, and the ripple output texture is
    // likewise written into the dummy set (WaterPipeline::updateDummyRipple). It is view-independent
    // — one camera-following patch shared by every view — so there is nothing to suppress.
    //
    // VK-1607: this mask is now load-bearing on the RTT path rather than aspirational. RTT views
    // bind the pipeline's dummy set 9, whose UBO was previously written exactly once with all-default
    // WaterExtendedParams (flags = 0), so a probe rendered with hex, shoaling, shore waves, ripples
    // and the water-body clip all off whatever this said. GPUDrivenRenderer::updateWater now calls
    // WaterPipeline::updateDummyParams every frame with a copy of the real params whose flags are
    // pre-masked by exactly this constant — so the dummy buffer can never re-enable SSR or
    // absorption, which are the two features that genuinely need the main view's set 9.
    constexpr uint32_t WATER_VIEW_FLAGS_ALL = 0xFFFFFFFFu;
    constexpr uint32_t WATER_VIEW_FLAGS_RTT = ~(WATER_FLAG_SSR | WATER_FLAG_ABSORPTION);

    // VK-1604: ocean-level visual parameters that no longer fit in the 128-byte push-constant
    // budget. std140 layout, bound as set 9 binding 2 (vertex | fragment — hex tiling runs in the
    // vertex stage). Hand-padded into 16-byte rows exactly like WaterCausticsResources::CausticParams:
    // glm is not force-aligned in this engine (alignof(glm::vec4) == 4), so no vec3 may appear here
    // or the C++ and GLSL layouts silently diverge.
    struct WaterExtendedParams
    {
        glm::vec4 absorptionCoeff{0.45f, 0.08f, 0.02f, 0.0f};   //   0  rgb = extinction 1/m, w pad
        glm::vec4 scatterColor{0.0f, 0.35f, 0.30f, 0.0f};       //  16  rgb, w pad
        glm::vec4 scatterCoeff{0.05f, 0.05f, 0.04f, 0.0f};      //  32  rgb = in-scatter 1/m, w pad

        float ssrIntensity = 1.0f;                              //  48
        float ssrMaxDistance = 60.0f;                           //  52  metres
        float ssrThickness = 0.35f;                             //  56  metres (range-scaled in shader)
        uint32_t ssrMaxSteps = 24u;                             //  60

        float ssrEdgeFadeStart = 0.85f;                         //  64
        float ssrRefineSteps = 5.0f;                            //  68
        float absorptionMaxDistance = 30.0f;                    //  72  metres
        float hexBlendExponent = 4.0f;                          //  76

        float hexCellScale0 = 1.0f;                             //  80  hex cells per band patch
        float hexCellScale1 = 1.0f;                             //  84
        float hexCellScale2 = 1.0f;                             //  88
        uint32_t hexPerBandMask = 0x6u;                         //  92  default: bands 1 and 2

        uint32_t flags = 0u;                                    //  96
        float shoalingStrength = 0.0f;                          // 100  VK-1605  0 = no shoaling
        float shoalingGamma = 0.78f;                            // 104  VK-1605  McCowan H/d limit
        float shoreEdgeFadeStart = 0.88f;                       // 108  VK-1605  window fade start

        // VK-1606. xy = the ripple patch's min corner in world XZ (texel-snapped), z = patch size in
        // metres, w = 1/z. Same shape as shoreFieldOrigin below, and for the same reason: the shader
        // multiplies rather than divides.
        glm::vec4 ripplePatch{0.0f, 0.0f, 1.0f, 1.0f};           // 112

        // VK-1605. shoreFieldOrigin: xy = the shore-depth window's min corner in world XZ,
        // z = window size in metres, w = 1/z (the shader multiplies rather than divides).
        glm::vec4 shoreFieldOrigin{0.0f, 0.0f, 1.0f, 1.0f};      // 128
        // xyz = each band's characteristic wavelength in metres (Pierson-Moskowitz peak from its
        // own wind speed, times the user's scale), w = shoalingMinDepth.
        glm::vec4 bandWavelength{0.0f, 0.0f, 0.0f, 0.0f};        // 144
        // x = amplitude, y = length (metres of depth per crest), z = speed (crests/s),
        // w = breakDepth.
        glm::vec4 shoreWaveA{0.0f, 12.0f, 0.35f, 1.5f};          // 160
        // x = breakRange, y = crestFoam, z = crestFoamThreshold, w = shoreLean.
        glm::vec4 shoreWaveB{1.0f, 0.6f, 0.55f, 0.5f};           // 176

        // VK-1606. x = ripple height scale (metres per unit of simulated height), y = normal scale,
        // z = foam scale, w = patch-border fade start in 0..1 (same meaning as shoreEdgeFadeStart).
        glm::vec4 rippleParams{1.0f, 1.0f, 1.0f, 0.85f};         // 192

        // VK-1607. Water-body footprints the ocean must not draw inside: xy = min corner XZ,
        // zw = max corner XZ. std140 gives a vec4 array a 16-byte stride, which is what the C++
        // array already has, so the two layouts match without extra padding. Only the
        // bodyClipCount nearest the camera fit; the rest simply do not suppress the ocean.
        glm::vec4 bodyClipRects[::water::MAX_WATER_BODY_CLIP_RECTS]{};  // 208 .. 335
        uint32_t bodyClipCount = 0u;                            // 336
        float pad1607[3]{};                                     // 340 .. 351
    };
    static_assert(sizeof(WaterExtendedParams) == 352);

    // Vertex format for the subdivided unit quad
    struct WaterVertex
    {
        glm::vec3 position;             // 12 bytes
        glm::vec2 texCoord;             // 8 bytes
    };
    static_assert(sizeof(WaterVertex) == 20);

    constexpr uint32_t MAX_OCEAN_GPU_INSTANCES = ::water::MAX_WATER_GPU_INSTANCES;
    constexpr uint32_t WATER_DEFAULT_SUBDIVISIONS = 64;

    // Per-LOD mesh info (returned by WaterMeshBuffer)
    struct WaterLODMeshInfo
    {
        uint32_t vertexOffset = 0;
        uint32_t indexOffset = 0;
        uint32_t indexCount = 0;
        uint32_t vertexCount = 0;
        uint32_t subdivisions = 0;
    };
}
