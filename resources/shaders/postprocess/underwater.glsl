#type VERTEX
#version 460 core
#include "fullscreen_vert.glsl"

#type FRAGMENT
#version 460 core

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D inputTexture;

layout(push_constant) uniform PushConstants {
    float waterHeight;
    float cameraDepth;
    float submersionFactor;
    float time;
    float fogDensity;
    float fogColorR, fogColorG, fogColorB;
    float absorptionR, absorptionG, absorptionB;
    float causticStrength;
    float causticScale;
    float causticSpeed;
    float meniscusWidth;
    float meniscusDistortion;
    float chromaticStrength;
    float maxFogDistance;
    float nearPlane;
    float farPlane;
} pc;

// Procedural caustics via Voronoi noise
float voronoiNoise(vec2 uv) {
    vec2 i = floor(uv);
    vec2 f = fract(uv);
    float minDist = 1.0;
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            vec2 neighbor = vec2(float(x), float(y));
            vec2 point = fract(sin(vec2(dot(i + neighbor, vec2(127.1, 311.7)),
                                        dot(i + neighbor, vec2(269.5, 183.3)))) * 43758.5453);
            float dist = length(neighbor + point - f);
            minDist = min(minDist, dist);
        }
    }
    return minDist;
}

void main() {
    vec3 color = texture(inputTexture, texCoord).rgb;

    if (pc.submersionFactor <= 0.0) {
        outColor = vec4(color, 1.0);
        return;
    }

    // Meniscus line at waterline transition
    if (pc.submersionFactor > 0.0 && pc.submersionFactor < 1.0) {
        float waterLine = 1.0 - pc.submersionFactor;
        float meniscusDist = abs(texCoord.y - waterLine);
        if (meniscusDist < pc.meniscusWidth) {
            float distortAmount = (1.0 - meniscusDist / pc.meniscusWidth) * pc.meniscusDistortion;
            vec2 distortedUV = texCoord + vec2(sin(texCoord.y * 50.0 + pc.time * 3.0) * distortAmount, 0.0);
            distortedUV = clamp(distortedUV, vec2(0.001), vec2(0.999));
            color = texture(inputTexture, distortedUV).rgb;
        }
    }

    // Apply effects only to the submerged portion of the screen
    float pixelSubmersion = (texCoord.y > (1.0 - pc.submersionFactor)) ? 1.0 : 0.0;
    if (pc.submersionFactor >= 1.0) pixelSubmersion = 1.0;

    if (pixelSubmersion > 0.0) {
        // Color absorption (Beer-Lambert) -- simple distance approximation using cameraDepth
        float dist = pc.cameraDepth * 3.0; // scale for visual impact
        vec3 absorption = vec3(pc.absorptionR, pc.absorptionG, pc.absorptionB);
        color *= exp(-absorption * dist);

        // Underwater fog
        vec3 fogColor = vec3(pc.fogColorR, pc.fogColorG, pc.fogColorB);
        float fogFactor = 1.0 - exp(-pc.fogDensity * min(dist, pc.maxFogDistance));
        color = mix(color, fogColor, fogFactor);

        // Procedural caustics from above
        vec2 causticUV = texCoord * pc.causticScale;
        float c1 = voronoiNoise(causticUV + vec2(pc.time * pc.causticSpeed, 0.0));
        float c2 = voronoiNoise(causticUV * 1.5 + vec2(0.0, pc.time * pc.causticSpeed * 0.7));
        float caustic = pow(1.0 - min(c1, c2), 3.0);
        float causticFalloff = exp(-pc.cameraDepth * 0.5);
        color += caustic * pc.causticStrength * causticFalloff * fogColor;

        // Chromatic aberration
        if (pc.chromaticStrength > 0.0) {
            vec2 dir = texCoord - vec2(0.5);
            float r = texture(inputTexture, texCoord + dir * pc.chromaticStrength).r;
            float b = texture(inputTexture, texCoord - dir * pc.chromaticStrength).b;
            color.r = mix(color.r, r, 0.3);
            color.b = mix(color.b, b, 0.3);
        }
    }

    // Smooth blend with submersionFactor for transition
    vec3 originalColor = texture(inputTexture, texCoord).rgb;
    color = mix(originalColor, color, pc.submersionFactor);

    outColor = vec4(color, 1.0);
}
