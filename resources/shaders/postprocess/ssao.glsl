#type VERTEX
#version 460 core
#extension GL_GOOGLE_include_directive : require

#include "fullscreen_vert.glsl"

#type FRAGMENT
#version 460 core

layout(location = 0) in vec2 texCoord;
layout(location = 0) out float outOcclusion;

layout(set = 0, binding = 0) uniform sampler2D sceneColorTexture;
layout(set = 1, binding = 0) uniform sampler2D depthTexture;
layout(set = 1, binding = 1) uniform SSAOParams {
    mat4 projection;
    mat4 inverseProjection;
    vec4 params;         // radius, bias, intensity, power
    vec2 noiseScale;
    int kernelSize;
    float nearPlane;
    float farPlane;
    // C++ struct has 3 floats of padding here (12 bytes);
    // std140 rounds block size to vec4 alignment (176 bytes total), matching C++ sizeof
} ssao;

float linearizeDepth(float d)
{
    return ssao.nearPlane * ssao.farPlane / (ssao.farPlane - d * (ssao.farPlane - ssao.nearPlane));
}

vec3 reconstructViewPos(vec2 uv, float depth)
{
    vec4 clipPos = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 viewPos = ssao.inverseProjection * clipPos;
    return viewPos.xyz / viewPos.w;
}

float hash(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

vec2 hash2(vec2 p)
{
    return vec2(hash(p), hash(p + vec2(127.1, 311.7)));
}

vec3 generateSample(int index, vec2 noise)
{
    float fi = float(index);
    // Use interleaved gradient noise + golden ratio for distribution
    float angle = (fi * 2.399963 + noise.x * 6.283185);
    float radius = (fi + noise.y) / float(ssao.kernelSize);
    radius = mix(0.1, 1.0, radius * radius); // Quadratic distribution: more near center

    float cosAngle = cos(angle);
    float sinAngle = sin(angle);

    // Hemisphere: z always positive
    float z = (fi + 0.5) / float(ssao.kernelSize);
    float xy = sqrt(1.0 - z * z);

    return vec3(cosAngle * xy * radius, sinAngle * xy * radius, z * radius);
}

void main()
{
    float depth = texture(depthTexture, texCoord).r;

    if (depth >= 1.0)
    {
        outOcclusion = 1.0;
        return;
    }

    vec3 viewPos = reconstructViewPos(texCoord, depth);

    vec3 dPdx = dFdx(viewPos);
    vec3 dPdy = dFdy(viewPos);
    vec3 normal = normalize(cross(dPdy, dPdx));

    vec2 pixelCoord = gl_FragCoord.xy;
    vec2 noise = hash2(pixelCoord) * 2.0 - 1.0;

    vec3 tangent = normalize(noise.x * dPdx + noise.y * dPdy);
    tangent = normalize(tangent - normal * dot(tangent, normal)); // Gram-Schmidt
    vec3 bitangent = cross(normal, tangent);
    mat3 TBN = mat3(tangent, bitangent, normal);

    float radius = ssao.params.x;
    float bias = ssao.params.y;

    float occlusion = 0.0;
    int baseSamples = min(ssao.kernelSize, 64);

    // Distance-based sample reduction: fewer samples for distant pixels where SSAO detail is less visible
    float linearDepth = linearizeDepth(depth);
    float distanceFactor = clamp(linearDepth / ssao.farPlane, 0.0, 1.0);
    int samples = max(8, int(float(baseSamples) * (1.0 - distanceFactor * 0.5)));

    for (int i = 0; i < samples; ++i)
    {
        vec3 sampleDir = TBN * generateSample(i, hash2(pixelCoord + vec2(float(i))));
        vec3 samplePos = viewPos + sampleDir * radius;

        vec4 offset = ssao.projection * vec4(samplePos, 1.0);
        offset.xyz /= offset.w;
        offset.xy = offset.xy * 0.5 + 0.5;

        float sampleDepth = texture(depthTexture, offset.xy).r;
        vec3 sampleViewPos = reconstructViewPos(offset.xy, sampleDepth);

        // Range check: attenuate contribution based on distance
        float rangeCheck = smoothstep(0.0, 1.0, radius / abs(viewPos.z - sampleViewPos.z));

        occlusion += (sampleViewPos.z >= samplePos.z + bias ? 1.0 : 0.0) * rangeCheck;
    }

    occlusion = 1.0 - (occlusion / float(samples));
    outOcclusion = pow(occlusion, ssao.params.w);
}
