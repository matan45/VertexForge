#ifndef WATER_PARAMS_GLSL
#define WATER_PARAMS_GLSL

// VK-1604: extended water parameters, set 9 binding 2 (vertex | fragment).
// MUST stay byte-for-byte in sync with render::water::WaterExtendedParams in
// VFEngine/graphics/render/water/WaterGPUTypes.hpp — there is no codegen between them.
// The C++ side is hand-padded into 16-byte rows and static_asserts sizeof() == 208.

#define WATER_FLAG_SSR         1u
#define WATER_FLAG_ABSORPTION  2u
#define WATER_FLAG_HEX         4u
#define WATER_FLAG_SSR_DEBUG   8u
#define WATER_FLAG_SHOALING    16u
#define WATER_FLAG_SHORE_WAVES 32u
#define WATER_FLAG_SHORE_FIELD 64u
#define WATER_FLAG_RIPPLES     128u    // VK-1606
#define WATER_FLAG_BODY_CLIP   256u    // VK-1607: ocean tiles discard inside bodyClipRects
// bits 9..15 still free

// VK-1607: per-tile flag bits carried in WaterTileData::heightAndWave.w.
// Twin: water::WATER_TILE_BAND_MASK_BITS / WATER_TILE_IS_BODY in WaterTileGrid.hpp.
#define WATER_TILE_BAND_MASK_BITS 7u
#define WATER_TILE_IS_BODY        8u

layout(std140, set = 9, binding = 2) uniform WaterExtendedParamsUBO {
    vec4 absorptionCoeff;       //   0  rgb = extinction 1/m
    vec4 scatterColor;          //  16  rgb
    vec4 scatterCoeff;          //  32  rgb = in-scatter 1/m

    float ssrIntensity;         //  48
    float ssrMaxDistance;       //  52  metres
    float ssrThickness;         //  56  metres (range-scaled at use)
    uint  ssrMaxSteps;          //  60

    float ssrEdgeFadeStart;     //  64
    float ssrRefineSteps;       //  68
    float absorptionMaxDistance;//  72  metres
    float hexBlendExponent;     //  76

    float hexCellScale0;        //  80
    float hexCellScale1;        //  84
    float hexCellScale2;        //  88
    uint  hexPerBandMask;       //  92

    uint  flags;                //  96
    float shoalingStrength;     // 100  VK-1605
    float shoalingGamma;        // 104  VK-1605  McCowan H/d breaking limit
    float shoreEdgeFadeStart;   // 108  VK-1605  window fade start, 0..1

    vec4  ripplePatch;          // 112  VK-1606 xy = patch min corner XZ, z = patchSize, w = 1/patchSize

    vec4  shoreFieldOrigin;     // 128  xy = window min corner XZ, z = windowSize, w = 1/windowSize
    vec4  bandWavelength;       // 144  xyz = per-band characteristic lambda (m), w = shoalingMinDepth
    vec4  shoreWaveA;           // 160  x amplitude, y length, z speed, w breakDepth
    vec4  shoreWaveB;           // 176  x breakRange, y crestFoam, z crestFoamThreshold, w shoreLean
    vec4  rippleParams;         // 192  VK-1606 x heightScale, y normalScale, z foamScale, w edgeFadeStart

    vec4  bodyClipRects[8];     // 208  VK-1607 xy = min corner XZ, zw = max corner XZ
    uint  bodyClipCount;        // 336
    float pad1607a;             // 340
    float pad1607b;             // 344
    float pad1607c;             // 348
} ext;                          // 352

#endif // WATER_PARAMS_GLSL
