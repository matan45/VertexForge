#type VERTEX
#version 460 core
#extension GL_GOOGLE_include_directive : require

#include "../postprocess/fullscreen_vert.glsl"

#type FRAGMENT
#version 460 core
#extension GL_GOOGLE_include_directive : require

#include "../common/hiz_ray_march.glsl"

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D sceneColorTexture;
layout(set = 1, binding = 0) uniform sampler2D depthTexture;
layout(set = 1, binding = 1) uniform sampler2D hiZTexture;
layout(set = 1, binding = 2) uniform sampler2D normalRoughnessTexture;
layout(set = 1, binding = 3) uniform SSRParams {
    mat4 projection;
    mat4 inverseProjection;
    mat4 view;
    mat4 inverseView;
    mat4 prevViewProjection;
    vec4 params;           // maxDistance, intensity, roughnessThreshold, edgeFadeStart
    vec2 resolution;
    vec2 texelSize;
    float nearPlane;
    float farPlane;
    uint maxSteps;
    uint frameIndex;
    uint historyValid;
    uint halfResolution;
    float temporalBlend;
    float padding;
} ssr;

vec3 reconstructViewPos(vec2 uv, float depth)
{
    vec4 clipPos = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 viewPos = ssr.inverseProjection * clipPos;
    return viewPos.xyz / viewPos.w;
}

float hash(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

// Importance-sample GGX for roughness-based ray jittering
vec3 importanceSampleGGX(vec2 xi, vec3 N, float roughness)
{
    float a = roughness * roughness;
    float phi = 2.0 * 3.14159265 * xi.x;
    float cosTheta = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    vec3 H = vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);

    // Build TBN from N
    vec3 up = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, N));
    vec3 bitangent = cross(N, tangent);

    return normalize(tangent * H.x + bitangent * H.y + N * H.z);
}

void main()
{
    float depth = texture(depthTexture, texCoord).r;

    if (depth >= 1.0)
    {
        outColor = vec4(0.0);
        return;
    }

    // Read normal (world-space) and roughness from depth prepass
    vec4 normalRoughness = texture(normalRoughnessTexture, texCoord);
    vec3 worldNormal = normalize(normalRoughness.xyz);
    float roughness = normalRoughness.w;

    float roughnessThreshold = ssr.params.z;

    // Skip very rough surfaces
    if (roughness > roughnessThreshold)
    {
        outColor = vec4(0.0);
        return;
    }

    // Reconstruct view-space position and normal
    vec3 viewPos = reconstructViewPos(texCoord, depth);
    vec3 viewNormal = normalize(mat3(ssr.view) * worldNormal);

    // View direction in view space (from surface toward camera)
    vec3 viewDir = normalize(-viewPos);

    // Reflection direction in view space
    vec3 reflectDir = reflect(-viewDir, viewNormal);

    // Skip reflections going toward camera (behind surface)
    if (reflectDir.z > 0.0)
    {
        outColor = vec4(0.0);
        return;
    }

    // Jitter reflection direction for rough surfaces
    if (roughness > 0.01)
    {
        vec2 pixelCoord = gl_FragCoord.xy;
        vec2 noise = vec2(
            hash(pixelCoord + float(ssr.frameIndex) * 1.618),
            hash(pixelCoord + float(ssr.frameIndex) * 2.718)
        );
        vec3 H = importanceSampleGGX(noise, viewNormal, roughness);
        reflectDir = reflect(-viewDir, H);

        // Reject if jittered direction goes toward camera
        if (reflectDir.z > 0.0)
        {
            outColor = vec4(0.0);
            return;
        }
    }

    float maxDistance = ssr.params.x;
    float edgeFadeStart = ssr.params.w;

    HiZRayResult rayResult = hiZRayMarch(
        viewPos,
        reflectDir,
        ssr.projection,
        ssr.inverseProjection,
        hiZTexture,
        depthTexture,
        ssr.resolution,
        ssr.nearPlane,
        ssr.farPlane,
        ssr.maxSteps,
        maxDistance,
        edgeFadeStart
    );

    if (rayResult.hit)
    {
        vec3 hitColor = texture(sceneColorTexture, rayResult.hitUV).rgb;

        // Roughness-based confidence falloff — only fade near the threshold
        float roughnessFade = 1.0 - smoothstep(roughnessThreshold * 0.5, roughnessThreshold, roughness);

        float confidence = rayResult.confidence * roughnessFade;
        outColor = vec4(hitColor, confidence);
    }
    else
    {
        outColor = vec4(0.0);
    }
}
