#ifndef SHADOW_SAMPLING_GLSL
#define SHADOW_SAMPLING_GLSL

// Shadow Data Structure (must match GPUShadowData in ShadowTypes.hpp - 128 bytes)
// Actual sampling functions are implemented in each shader that uses them
// because GLSL doesn't support passing unsized arrays as function parameters.

struct ShadowData {
    mat4 viewProjection;
    vec4 atlasViewport;
    vec4 biasParams;
    vec4 rangeParams;
    vec4 pcfParams;
};

#endif // SHADOW_SAMPLING_GLSL
