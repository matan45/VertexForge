#type VERTEX
#version 460 core
#extension GL_GOOGLE_include_directive : require

#include "../postprocess/fullscreen_vert.glsl"

#type FRAGMENT
#version 460 core

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D sceneColorTexture;
layout(set = 1, binding = 0) uniform sampler2D depthTexture;
layout(set = 1, binding = 1) uniform SSGIParams {
    mat4 projection;
    mat4 inverseProjection;
    mat4 view;
    mat4 inverseView;
    mat4 prevViewProjection;
    vec4 params;           // radius, maxDistance, intensity, temporalBlend
    vec2 resolution;
    vec2 texelSize;
    float nearPlane;
    float farPlane;
    uint sampleCount;
    uint frameIndex;
    uint historyValid;
    uint halfResolution;
} ssgi;

vec3 reconstructViewPos(vec2 uv, float depth)
{
    vec4 clipPos = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 viewPos = ssgi.inverseProjection * clipPos;
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

// Cosine-weighted hemisphere sample
vec3 cosineWeightedSample(int index, uint totalSamples, vec2 noise)
{
    float fi = float(index);
    float angle = (fi * 2.399963 + noise.x * 6.283185);
    float r = sqrt((fi + noise.y) / float(totalSamples));

    float x = cos(angle) * r;
    float y = sin(angle) * r;
    float z = sqrt(max(0.0, 1.0 - r * r));

    return vec3(x, y, z);
}

// Screen-space ray march
bool rayMarch(vec3 viewOrigin, vec3 viewDir, out vec2 hitUV, out float hitDepth)
{
    float radius = ssgi.params.x;
    vec3 viewEnd = viewOrigin + viewDir * radius;

    vec4 clipStart = ssgi.projection * vec4(viewOrigin, 1.0);
    vec4 clipEnd = ssgi.projection * vec4(viewEnd, 1.0);

    vec2 screenStart = (clipStart.xy / clipStart.w) * 0.5 + 0.5;
    vec2 screenEnd = (clipEnd.xy / clipEnd.w) * 0.5 + 0.5;

    vec2 screenDir = screenEnd - screenStart;
    float screenDist = length(screenDir * ssgi.resolution);
    int stepCount = clamp(int(screenDist), 4, 64);

    float invStartW = 1.0 / clipStart.w;
    float invEndW = 1.0 / clipEnd.w;
    vec2 startOverW = screenStart * invStartW;
    vec2 endOverW = screenEnd * invEndW;
    float originZOverW = viewOrigin.z * invStartW;
    float endZOverW = viewEnd.z * invEndW;

    for (int step = 1; step <= stepCount; step++)
    {
        float t = float(step) / float(stepCount);

        float invW = mix(invStartW, invEndW, t);
        vec2 sampleUV = mix(startOverW, endOverW, t) / invW;
        float rayZ = mix(originZOverW, endZOverW, t) / invW;

        if (any(lessThan(sampleUV, vec2(0.001))) || any(greaterThan(sampleUV, vec2(0.999))))
            return false;

        float sceneDepth = texture(depthTexture, sampleUV).r;
        if (sceneDepth >= 1.0) continue;

        vec3 sceneViewPos = reconstructViewPos(sampleUV, sceneDepth);

        float thickness = 0.5;
        if (rayZ < sceneViewPos.z && rayZ > sceneViewPos.z - thickness)
        {
            hitUV = sampleUV;
            hitDepth = sceneDepth;
            return true;
        }
    }
    return false;
}

void main()
{
    float depth = texture(depthTexture, texCoord).r;

    if (depth >= 1.0)
    {
        outColor = vec4(0.0);
        return;
    }

    vec3 viewPos = reconstructViewPos(texCoord, depth);

    // Reconstruct normal from depth derivatives
    vec3 dPdx = dFdx(viewPos);
    vec3 dPdy = dFdy(viewPos);
    vec3 normal = normalize(cross(dPdy, dPdx));

    // Build TBN
    vec2 pixelCoord = gl_FragCoord.xy;
    vec2 noise = hash2(pixelCoord + float(ssgi.frameIndex) * 1.618) * 2.0 - 1.0;

    vec3 tangent = normalize(noise.x * dPdx + noise.y * dPdy);
    tangent = normalize(tangent - normal * dot(tangent, normal));
    vec3 bitangent = cross(normal, tangent);
    mat3 TBN = mat3(tangent, bitangent, normal);

    vec3 indirectSum = vec3(0.0);
    float weightSum = 0.0;
    float maxDist = ssgi.params.y;
    uint samples = min(ssgi.sampleCount, 16u);

    for (uint i = 0; i < samples; i++)
    {
        vec2 sampleNoise = hash2(pixelCoord + vec2(float(i), float(ssgi.frameIndex)));
        vec3 dir = TBN * cosineWeightedSample(int(i), samples, sampleNoise);

        vec2 hitUV;
        float hitDepth;
        if (rayMarch(viewPos, dir, hitUV, hitDepth))
        {
            vec3 hitColor = texture(sceneColorTexture, hitUV).rgb;

            vec3 hitViewPos = reconstructViewPos(hitUV, hitDepth);
            float dist = length(hitViewPos - viewPos);
            float atten = 1.0 - smoothstep(0.0, maxDist, dist);

            indirectSum += hitColor * atten;
            weightSum += 1.0;
        }
    }

    vec3 indirect = weightSum > 0.0 ? indirectSum / weightSum : vec3(0.0);
    float confidence = weightSum / float(samples);
    outColor = vec4(indirect, confidence);
}
