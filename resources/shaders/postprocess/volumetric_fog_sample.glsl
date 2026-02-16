#type VERTEX
#version 460 core
#extension GL_GOOGLE_include_directive : require

#include "fullscreen_vert.glsl"

#type FRAGMENT
#version 460 core

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 outColor;

// Binding 0: Depth texture
layout(set = 0, binding = 0) uniform sampler2D depthTexture;

// Binding 1: 3D integrated volume from compute passes
layout(set = 0, binding = 1) uniform sampler3D integratedVolume;

// Binding 2: Composite parameters
layout(set = 0, binding = 2) uniform VolumetricCompositeParams {
    float nearPlane;
    float farPlane;
    float intensity;
    float padding0;
    uvec4 gridDimensions; // xyz = width, height, depth
} params;

// Logarithmic depth distribution (must match compute shaders)
float depthToSlice(float depth, float near, float far, float numSlices) {
    float logRatio = log(depth / near) / log(far / near);
    return clamp(logRatio * numSlices, 0.0, numSlices - 1.0);
}

float linearizeDepth(float windowZ, float near, float far) {
    float denominator = max(far - windowZ * (far - near), 0.0001);
    return near * far / denominator;
}

void main()
{
    float rawDepth = texture(depthTexture, texCoord).r;

    // Linearize depth
    float linearZ = linearizeDepth(rawDepth, params.nearPlane, params.farPlane);

    // Convert to froxel UV
    float slice = depthToSlice(linearZ, params.nearPlane, params.farPlane, float(params.gridDimensions.z));
    float w = (slice + 0.5) / float(params.gridDimensions.z);

    vec3 volumeUV = vec3(texCoord, w);

    // Sample integrated volume (trilinear filtering)
    vec4 volumeData = texture(integratedVolume, volumeUV);

    vec3 inScattered = volumeData.rgb * params.intensity;
    float transmittance = volumeData.a;

    // Output: scattering.rgb + transmittance in alpha
    outColor = vec4(inScattered, transmittance);
}
