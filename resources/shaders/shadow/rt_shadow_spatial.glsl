#type COMPUTE
#version 460

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 0, r16f) uniform readonly  image2D inputShadow;
layout(set = 0, binding = 1, r16f) uniform writeonly image2D outputShadow;
layout(set = 0, binding = 2) uniform sampler2D depthBuffer;
layout(set = 0, binding = 3) uniform sampler2D normalBuffer;

layout(push_constant) uniform SpatialParams {
    int stepSize;
    float phiDepth;
    float phiNormal;
    uint passIndex;
};

// 5x5 à-trous kernel weights (B3 spline)
const float kernel[3] = float[](1.0, 2.0 / 3.0, 1.0 / 6.0);

void main() {
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 screenSize = imageSize(inputShadow);

    if (pixel.x >= screenSize.x || pixel.y >= screenSize.y)
        return;

    vec2 texelSize = 1.0 / vec2(screenSize);
    vec2 uv = (vec2(pixel) + 0.5) * texelSize;

    float centerShadow = imageLoad(inputShadow, pixel).r;
    float centerDepth = texture(depthBuffer, uv).r;

    // Sky: pass through
    if (centerDepth >= 1.0 || centerDepth <= 0.0) {
        imageStore(outputShadow, pixel, vec4(centerShadow));
        return;
    }

    vec3 centerNormal = normalize(texture(normalBuffer, uv).xyz);

    float sumWeight = 1.0;
    float sumShadow = centerShadow;

    for (int dy = -2; dy <= 2; ++dy) {
        for (int dx = -2; dx <= 2; ++dx) {
            if (dx == 0 && dy == 0) continue;

            ivec2 offset = ivec2(dx, dy) * stepSize;
            ivec2 samplePos = clamp(pixel + offset, ivec2(0), screenSize - 1);
            vec2 sampleUV = (vec2(samplePos) + 0.5) * texelSize;

            float sampleShadow = imageLoad(inputShadow, samplePos).r;
            float sampleDepth = texture(depthBuffer, sampleUV).r;
            vec3 sampleNormal = normalize(texture(normalBuffer, sampleUV).xyz);

            // Edge-stopping: depth
            float wDepth = exp(-abs(centerDepth - sampleDepth) / (phiDepth + 1e-6));

            // Edge-stopping: normal
            float wNormal = pow(max(dot(centerNormal, sampleNormal), 0.0), phiNormal);

            // Kernel weight
            float wKernel = kernel[abs(dx)] * kernel[abs(dy)];

            float w = wKernel * wDepth * wNormal;
            sumShadow += sampleShadow * w;
            sumWeight += w;
        }
    }

    imageStore(outputShadow, pixel, vec4(sumShadow / sumWeight));
}
