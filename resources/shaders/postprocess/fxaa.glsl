#type VERTEX
#version 460 core
#extension GL_GOOGLE_include_directive : require

#include "fullscreen_vert.glsl"

#type FRAGMENT
#version 460 core

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D inputTexture;

layout(push_constant) uniform PushConstants {
    float texelSizeX;
    float texelSizeY;
    float edgeThresholdMin;
    float edgeThreshold;
    uint quality; // 0=Low, 1=Medium, 2=High
} pc;

float luminance(vec3 color)
{
    return dot(color, vec3(0.299, 0.587, 0.114));
}

void main()
{
    vec2 texelSize = vec2(pc.texelSizeX, pc.texelSizeY);

    // Sample center and cardinal neighbors
    vec3 rgbM  = texture(inputTexture, texCoord).rgb;
    vec3 rgbN  = texture(inputTexture, texCoord + vec2( 0.0, -texelSize.y)).rgb;
    vec3 rgbS  = texture(inputTexture, texCoord + vec2( 0.0,  texelSize.y)).rgb;
    vec3 rgbE  = texture(inputTexture, texCoord + vec2( texelSize.x,  0.0)).rgb;
    vec3 rgbW  = texture(inputTexture, texCoord + vec2(-texelSize.x,  0.0)).rgb;

    float lumaM = luminance(rgbM);
    float lumaN = luminance(rgbN);
    float lumaS = luminance(rgbS);
    float lumaE = luminance(rgbE);
    float lumaW = luminance(rgbW);

    float lumaMin = min(lumaM, min(min(lumaN, lumaS), min(lumaE, lumaW)));
    float lumaMax = max(lumaM, max(max(lumaN, lumaS), max(lumaE, lumaW)));
    float lumaRange = lumaMax - lumaMin;

    // Early exit if contrast is below threshold
    if (lumaRange < max(pc.edgeThresholdMin, lumaMax * pc.edgeThreshold))
    {
        outColor = vec4(rgbM, 1.0);
        return;
    }

    // Sample diagonal neighbors
    vec3 rgbNW = texture(inputTexture, texCoord + vec2(-texelSize.x, -texelSize.y)).rgb;
    vec3 rgbNE = texture(inputTexture, texCoord + vec2( texelSize.x, -texelSize.y)).rgb;
    vec3 rgbSW = texture(inputTexture, texCoord + vec2(-texelSize.x,  texelSize.y)).rgb;
    vec3 rgbSE = texture(inputTexture, texCoord + vec2( texelSize.x,  texelSize.y)).rgb;

    float lumaNW = luminance(rgbNW);
    float lumaNE = luminance(rgbNE);
    float lumaSW = luminance(rgbSW);
    float lumaSE = luminance(rgbSE);

    // Subpixel aliasing factor
    float lumaAvg = (lumaN + lumaS + lumaE + lumaW) * 0.25;
    float subpixelOffset = clamp(abs(lumaAvg - lumaM) / lumaRange, 0.0, 1.0);
    subpixelOffset = smoothstep(0.0, 1.0, subpixelOffset);
    subpixelOffset = subpixelOffset * subpixelOffset * 0.75;

    // Determine edge direction
    float edgeH = abs(lumaNW + lumaNE - 2.0 * lumaN)
                + abs(lumaW  + lumaE  - 2.0 * lumaM) * 2.0
                + abs(lumaSW + lumaSE - 2.0 * lumaS);

    float edgeV = abs(lumaNW + lumaSW - 2.0 * lumaW)
                + abs(lumaN  + lumaS  - 2.0 * lumaM) * 2.0
                + abs(lumaNE + lumaSE - 2.0 * lumaE);

    bool isHorizontal = (edgeH >= edgeV);

    // Select edge normal direction
    float stepLength = isHorizontal ? texelSize.y : texelSize.x;
    float lumaP = isHorizontal ? lumaN : lumaE;
    float lumaN2 = isHorizontal ? lumaS : lumaW;

    float gradientP = abs(lumaP - lumaM);
    float gradientN = abs(lumaN2 - lumaM);

    bool pSteeper = gradientP >= gradientN;
    float gradientScaled = 0.25 * max(gradientP, gradientN);

    if (!pSteeper)
    {
        stepLength = -stepLength;
    }

    float lumaLocalAvg;
    if (pSteeper)
    {
        lumaLocalAvg = 0.5 * (lumaP + lumaM);
    }
    else
    {
        lumaLocalAvg = 0.5 * (lumaN2 + lumaM);
    }

    // Shift UV to edge
    vec2 currentUV = texCoord;
    if (isHorizontal)
    {
        currentUV.y += stepLength * 0.5;
    }
    else
    {
        currentUV.x += stepLength * 0.5;
    }

    // Determine search step count based on quality
    int searchSteps;
    if (pc.quality == 0u)
    {
        searchSteps = 4;  // Low
    }
    else if (pc.quality == 1u)
    {
        searchSteps = 8;  // Medium
    }
    else
    {
        searchSteps = 12; // High
    }

    // Search along edge in both directions
    vec2 edgeStep = isHorizontal ? vec2(texelSize.x, 0.0) : vec2(0.0, texelSize.y);

    vec2 uvP = currentUV + edgeStep;
    vec2 uvN = currentUV - edgeStep;

    float lumaEndP = luminance(texture(inputTexture, uvP).rgb) - lumaLocalAvg;
    float lumaEndN = luminance(texture(inputTexture, uvN).rgb) - lumaLocalAvg;

    bool reachedP = abs(lumaEndP) >= gradientScaled;
    bool reachedN = abs(lumaEndN) >= gradientScaled;

    for (int i = 1; i < searchSteps && !(reachedP && reachedN); i++)
    {
        if (!reachedP)
        {
            uvP += edgeStep;
            lumaEndP = luminance(texture(inputTexture, uvP).rgb) - lumaLocalAvg;
            reachedP = abs(lumaEndP) >= gradientScaled;
        }
        if (!reachedN)
        {
            uvN -= edgeStep;
            lumaEndN = luminance(texture(inputTexture, uvN).rgb) - lumaLocalAvg;
            reachedN = abs(lumaEndN) >= gradientScaled;
        }
    }

    // Compute distances to edge endpoints
    float distP, distN;
    if (isHorizontal)
    {
        distP = uvP.x - texCoord.x;
        distN = texCoord.x - uvN.x;
    }
    else
    {
        distP = uvP.y - texCoord.y;
        distN = texCoord.y - uvN.y;
    }

    float distMin = min(distP, distN);
    float edgeLength = distP + distN;
    float pixelOffset = -distMin / edgeLength + 0.5;

    // Check if the luma at the closer end is in the wrong direction
    bool isLumaCenterSmaller = lumaM < lumaLocalAvg;
    bool correctVariation = ((distP < distN) ? lumaEndP : lumaEndN) < 0.0;
    correctVariation = isLumaCenterSmaller != correctVariation;

    float finalOffset = correctVariation ? pixelOffset : 0.0;
    finalOffset = max(finalOffset, subpixelOffset);

    // Apply offset and sample
    vec2 finalUV = texCoord;
    if (isHorizontal)
    {
        finalUV.y += finalOffset * stepLength;
    }
    else
    {
        finalUV.x += finalOffset * stepLength;
    }

    vec3 finalColor = texture(inputTexture, finalUV).rgb;
    outColor = vec4(finalColor, 1.0);
}
