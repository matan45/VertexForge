#ifndef GPU_INSTANCE_TYPES_GLSL
#define GPU_INSTANCE_TYPES_GLSL

// Matches C++ GPUInstanceTransform (112 bytes, alignas(16)).
// Shared between task_gpudriven.glsl and shadow.glsl.
struct GPUInstanceTransform {
    mat4 modelMatrix;
    vec4 albedoOverride;
    vec4 pbrOverride;      // metallic, roughness, ao, emission
    vec4 iblOverride;      // iblDiffuse, iblSpecular, alphaCutoff, hasOverride
};

#endif // GPU_INSTANCE_TYPES_GLSL
