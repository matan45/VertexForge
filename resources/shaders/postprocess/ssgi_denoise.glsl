#type VERTEX
#version 460 core

layout(location = 0) out vec2 fragTexCoord;

void main() {
    fragTexCoord = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(fragTexCoord * 2.0 - 1.0, 0.0, 1.0);
}

#type FRAGMENT
#version 460 core

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D rawSSGI;
layout(set = 0, binding = 1) uniform sampler2D sceneDepth;
layout(set = 0, binding = 2) uniform DenoiseParams {
    mat4 projection;
    mat4 inverseProjection;
    mat4 viewMatrix;
    vec4 params;
    vec4 screenParams;
    float nearPlane;
    float farPlane;
    int stepCount;
    float frameRandom;
};

float linearizeDepth(float depth) {
    return nearPlane * farPlane / (farPlane - depth * (farPlane - nearPlane));
}

void main() {
    vec2 texelSize = screenParams.zw;
    float centerDepth = linearizeDepth(texture(sceneDepth, fragTexCoord).r);

    // Edge-preserving bilateral blur (5x5 kernel)
    vec3 result = vec3(0.0);
    float totalWeight = 0.0;

    const int KERNEL_RADIUS = 2;
    const float SIGMA_SPATIAL = 2.0;
    const float SIGMA_DEPTH = 0.5;

    for (int y = -KERNEL_RADIUS; y <= KERNEL_RADIUS; ++y) {
        for (int x = -KERNEL_RADIUS; x <= KERNEL_RADIUS; ++x) {
            vec2 offset = vec2(float(x), float(y)) * texelSize;
            vec2 sampleUV = fragTexCoord + offset;

            // Clamp to screen
            sampleUV = clamp(sampleUV, vec2(0.0), vec2(1.0));

            vec3 sampleColor = texture(rawSSGI, sampleUV).rgb;
            float sampleDepth = linearizeDepth(texture(sceneDepth, sampleUV).r);

            // Spatial weight (Gaussian)
            float dist2 = float(x * x + y * y);
            float spatialWeight = exp(-dist2 / (2.0 * SIGMA_SPATIAL * SIGMA_SPATIAL));

            // Depth weight (edge-preserving)
            float depthDiff = abs(centerDepth - sampleDepth);
            float depthWeight = exp(-depthDiff * depthDiff / (2.0 * SIGMA_DEPTH * SIGMA_DEPTH));

            float weight = spatialWeight * depthWeight;
            result += sampleColor * weight;
            totalWeight += weight;
        }
    }

    if (totalWeight > 0.0) {
        result /= totalWeight;
    }

    outColor = vec4(result, 1.0);
}
