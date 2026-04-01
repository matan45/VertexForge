#type VERTEX
#version 460 core
#include "fullscreen_vert.glsl"

#type FRAGMENT
#version 460 core

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D inputTexture;

layout(push_constant) uniform PushConstants {
    float intensity;
    float time;
    float cameraPitchDot;
    float dropletScale;
    float trailSpeed;
};

// Hash functions for procedural droplet placement
float hash21(vec2 p)
{
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float hash11(float p)
{
    p = fract(p * 0.1031);
    p *= p + 33.33;
    p *= p + p;
    return fract(p);
}

// Smooth droplet shape with trail
float droplet(vec2 uv, vec2 center, float size, float trailLen)
{
    vec2 d = uv - center;

    // Main droplet (circle)
    float drop = smoothstep(size, size * 0.5, length(d));

    // Trail going upward (opposite to gravity direction on screen)
    float trail = 0.0;
    if (d.y < 0.0)
    {
        float trailWidth = size * 0.4;
        float trailMask = smoothstep(trailWidth, 0.0, abs(d.x));
        float trailFade = smoothstep(-trailLen, 0.0, d.y);
        trail = trailMask * trailFade * 0.5;
    }

    return max(drop, trail);
}

// Generate a layer of droplets
float dropletLayer(vec2 uv, float layerScale, float timeOffset)
{
    float result = 0.0;
    vec2 gridUV = uv * layerScale;
    vec2 cellId = floor(gridUV);
    vec2 cellUV = fract(gridUV);

    for (int y = -1; y <= 1; y++)
    {
        for (int x = -1; x <= 1; x++)
        {
            vec2 neighbor = vec2(float(x), float(y));
            vec2 id = cellId + neighbor;

            float rnd = hash21(id);
            if (rnd > intensity * 0.8 + 0.2)
                continue;  // Skip based on intensity

            // Droplet lifetime cycle
            float lifecycle = fract(rnd * 7.0 + time * 0.15 + timeOffset);
            float fadeIn = smoothstep(0.0, 0.1, lifecycle);
            float fadeOut = smoothstep(1.0, 0.7, lifecycle);
            float alpha = fadeIn * fadeOut;

            if (alpha < 0.01)
                continue;

            // Droplet position within cell
            vec2 pos = vec2(hash21(id + 0.1), hash21(id + 0.2)) * 0.6 + 0.2;

            // Trail movement: droplets slide down over their lifetime
            float trailBias = (1.0 - cameraPitchDot) * 0.5;
            pos.y += lifecycle * trailSpeed * 0.3 * trailBias;
            pos = fract(pos);

            float size = (hash11(rnd * 123.0) * 0.3 + 0.1) * dropletScale / layerScale;
            float trailLen = size * 2.0 * lifecycle;

            float d = droplet(cellUV, pos + neighbor, size, trailLen);
            result += d * alpha;
        }
    }

    return clamp(result, 0.0, 1.0);
}

void main()
{
    if (intensity < 0.001)
    {
        outColor = texture(inputTexture, texCoord);
        return;
    }

    // Multiple layers at different scales for depth
    float layer1 = dropletLayer(texCoord, 8.0 * dropletScale, 0.0);
    float layer2 = dropletLayer(texCoord, 14.0 * dropletScale, 3.7);
    float layer3 = dropletLayer(texCoord, 22.0 * dropletScale, 7.3);

    float combinedDroplets = layer1 * 0.5 + layer2 * 0.3 + layer3 * 0.2;
    combinedDroplets *= intensity;

    // Refraction: offset UV by droplet presence for lens distortion
    vec2 refractionOffset = vec2(
        dFdx(combinedDroplets),
        dFdy(combinedDroplets)
    ) * 0.02;

    vec2 distortedUV = clamp(texCoord + refractionOffset, vec2(0.0), vec2(1.0));
    vec3 sceneColor = texture(inputTexture, distortedUV).rgb;

    // Slight darkening where droplets are
    float darken = 1.0 - combinedDroplets * 0.15;
    sceneColor *= darken;

    // Specular highlight on droplets (fake lens refraction)
    float highlight = pow(combinedDroplets, 3.0) * 0.3;
    sceneColor += vec3(highlight);

    outColor = vec4(sceneColor, 1.0);
}
