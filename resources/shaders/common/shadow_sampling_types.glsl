#ifndef SHADOW_SAMPLING_TYPES_GLSL
#define SHADOW_SAMPLING_TYPES_GLSL

// Shadow Data Structure (must match GPUShadowData in ShadowTypes.hpp - 128 bytes)
struct ShadowData {
    mat4 viewProjection;
    vec4 atlasViewport;
    vec4 biasParams;    // x=depthBias, y=slopeBias, z=normalBias, w=texelSize
    vec4 rangeParams;   // x=near, y=far, z=cascadeCount, w=cascadeIndex
    vec4 pcssParams;    // x=lightSize, y=searchRadius, z=filterEnabled, w=cubeMapIndex
};

#endif // SHADOW_SAMPLING_TYPES_GLSL
