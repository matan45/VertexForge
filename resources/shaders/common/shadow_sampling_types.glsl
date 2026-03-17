#ifndef SHADOW_SAMPLING_TYPES_GLSL
#define SHADOW_SAMPLING_TYPES_GLSL

// VSM Light Data Structure (must match GPUVSMLight in VSMTypes.hpp - 128 bytes)
struct ShadowData {
    mat4 viewProjection;
    vec4 biasParams;      // x=depthBias, y=slopeBias, z=normalBias, w=texelSize
    vec4 rangeParams;     // x=near, y=far, z=cascadeCount, w=cascadeIndex
    vec4 pcssParams;      // x=lightSize, y=searchRadius, z=filterEnabled, w=cubeMapIndex
    ivec4 pageTableInfo;  // x=pagesX, y=pagesY, z=pageTableOffset, w=lightType (0=dir,1=spot,2=point)
};

#endif // SHADOW_SAMPLING_TYPES_GLSL
