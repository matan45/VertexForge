#ifndef WATER_PARAMS_GLSL
#define WATER_PARAMS_GLSL

// VK-1604: extended water parameters, set 9 binding 2 (vertex | fragment).
// MUST stay byte-for-byte in sync with render::water::WaterExtendedParams in
// VFEngine/graphics/render/water/WaterGPUTypes.hpp — there is no codegen between them.
// The C++ side is hand-padded into 16-byte rows and static_asserts sizeof() == 128.

#define WATER_FLAG_SSR        1u
#define WATER_FLAG_ABSORPTION 2u
#define WATER_FLAG_HEX        4u
#define WATER_FLAG_SSR_DEBUG  8u
// bits 4..15 reserved for VK-1605 (shoaling / shore waves) and VK-1606 (ripples)

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
    float reservedShoreField;   // 100
    float reservedShoaling;     // 104
    float reservedShoreWaves;   // 108

    vec4  reserved0;            // 112
} ext;                          // 128

#endif // WATER_PARAMS_GLSL
