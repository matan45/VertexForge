#ifndef VOLUMETRIC_COMMON_GLSL
#define VOLUMETRIC_COMMON_GLSL

const float VOL_PI = 3.14159265359;

// Logarithmic depth slice distribution (matches cluster grid)
float sliceToDepth(float slice, float near, float far, float numSlices) {
    float t = slice / numSlices;
    return near * pow(far / near, t);
}

float depthToSlice(float depth, float near, float far, float numSlices) {
    float logRatio = log(depth / near) / log(far / near);
    return clamp(logRatio * numSlices, 0.0, numSlices - 1.0);
}

// g > 0: forward scattering, g < 0: back scattering, g = 0: isotropic
float henyeyGreenstein(float cosTheta, float g) {
    float g2 = g * g;
    float denom = 1.0 + g2 - 2.0 * g * cosTheta;
    return (1.0 - g2) / (4.0 * VOL_PI * pow(denom, 1.5));
}

vec3 froxelToWorld(ivec3 froxelCoord, uvec3 gridDims, vec4 depthParams, mat4 invViewProj) {
    vec2 uv = (vec2(froxelCoord.xy) + 0.5) / vec2(gridDims.xy);

    float near = depthParams.x;
    float far = depthParams.y;
    float depth = sliceToDepth(float(froxelCoord.z) + 0.5, near, far, float(gridDims.z));

    vec2 ndc = uv * 2.0 - 1.0;
    float ndcDepth = (far * (depth - near)) / (depth * (far - near));

    vec4 clipPos = vec4(ndc, ndcDepth, 1.0);
    vec4 worldPos = invViewProj * clipPos;
    return worldPos.xyz / worldPos.w;
}

float computeFogDensity(vec3 worldPos, float uniformDensity, float heightDensity,
                        float heightFalloff, float heightOffset) {
    float density = uniformDensity;

    // Height-based exponential fog
    float heightAboveOffset = worldPos.y - heightOffset;
    if (heightFalloff > 0.001) {
        density += heightDensity * exp(-heightFalloff * max(heightAboveOffset, 0.0));
    } else {
        density += heightDensity;
    }

    return max(density, 0.0);
}

#endif // VOLUMETRIC_COMMON_GLSL
