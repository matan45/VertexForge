#type VERTEX
#version 460 core
#extension GL_GOOGLE_include_directive : require

#include "fullscreen_vert.glsl"

#type FRAGMENT
#version 460 core

// VK-1661: FXAA 3.11 PC Quality (Timothy Lottes, NVIDIA), engine port. Statements follow FxaaPixelShader
// in order; its unrolled end-of-edge search is the loop below over a per-preset step table. Every fetch
// is explicit-LOD because the search runs in divergent control flow.

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D inputTexture;

// FXAAPushConstants in FXAAEffect.hpp.
layout(push_constant) uniform PushConstants {
    float subpixel;          // fxaaQualitySubpix [0, 1]
    float edgeThreshold;     // fxaaQualityEdgeThreshold [0.063, 0.333]
    float edgeThresholdMin;  // fxaaQualityEdgeThresholdMin [0.0312, 0.0833]
    uint quality;            // 0 Low = preset 10, 1 Medium = 12, 2 High = 29, 3 Ultra = 39
} pc;

// FXAA_QUALITY__P0..P11 per preset, in texels; searchStepCount[preset] = FXAA_QUALITY__PS.
const int maxSearchSteps = 12;
const int searchStepCount[4] = int[4](3, 5, 12, 12);
const float searchSteps[48] = float[48](
    1.5, 3.0, 12.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
    1.0, 1.5, 2.0, 4.0, 12.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
    1.0, 1.5, 2.0, 2.0, 2.0, 2.0, 2.0, 2.0, 2.0, 2.0, 4.0, 8.0,
    1.0, 1.0, 1.0, 1.0, 1.0, 1.5, 2.0, 2.0, 2.0, 2.0, 4.0, 8.0);

// The perceptual (gamma-2.0) luma FXAA 3.11 asks for. The stored post-tonemap values are display-linear - they
// reach the B8G8R8A8_SRGB swapchain through an sRGB encode (ImGui in the editor, vkCmdBlitImage in the
// runtime) - so this is the square root of their Rec.709 luminance rather than 3.11's Rec.601 dot product on
// gamma-encoded colour. Clamped, because anything above 1 displays as 1.
float fxaaLuma(vec3 rgb)
{
    return sqrt(dot(clamp(rgb, 0.0, 1.0), vec3(0.2126, 0.7152, 0.0722)));
}

float lumaAt(vec2 uv)
{
    return fxaaLuma(textureLod(inputTexture, uv, 0.0).rgb);
}

void main()
{
    vec2 posM = texCoord;
    vec2 rcpFrame = 1.0 / vec2(textureSize(inputTexture, 0));

    vec4 rgbyM = textureLod(inputTexture, posM, 0.0);
    float lumaM = fxaaLuma(rgbyM.rgb);
    float lumaS = fxaaLuma(textureLodOffset(inputTexture, posM, 0.0, ivec2( 0,  1)).rgb);
    float lumaE = fxaaLuma(textureLodOffset(inputTexture, posM, 0.0, ivec2( 1,  0)).rgb);
    float lumaN = fxaaLuma(textureLodOffset(inputTexture, posM, 0.0, ivec2( 0, -1)).rgb);
    float lumaW = fxaaLuma(textureLodOffset(inputTexture, posM, 0.0, ivec2(-1,  0)).rgb);

    float rangeMax = max(max(lumaN, lumaW), max(lumaE, max(lumaS, lumaM)));
    float rangeMin = min(min(lumaN, lumaW), min(lumaE, min(lumaS, lumaM)));
    float range = rangeMax - rangeMin;
    float rangeMaxClamped = max(pc.edgeThresholdMin, rangeMax * pc.edgeThreshold);
    // 3.11's early exit, plus !(range > 0.0): a flat (or NaN) block must not reach 1.0 / range below.
    if (range < rangeMaxClamped || !(range > 0.0))
    {
        outColor = vec4(rgbyM.rgb, 1.0);
        return;
    }

    float lumaNW = fxaaLuma(textureLodOffset(inputTexture, posM, 0.0, ivec2(-1, -1)).rgb);
    float lumaSE = fxaaLuma(textureLodOffset(inputTexture, posM, 0.0, ivec2( 1,  1)).rgb);
    float lumaNE = fxaaLuma(textureLodOffset(inputTexture, posM, 0.0, ivec2( 1, -1)).rgb);
    float lumaSW = fxaaLuma(textureLodOffset(inputTexture, posM, 0.0, ivec2(-1,  1)).rgb);

    // edgeHorz sums second differences along y (a horizontal edge), edgeVert along x.
    float lumaNS = lumaN + lumaS;
    float lumaWE = lumaW + lumaE;
    float subpixRcpRange = 1.0 / range;
    float subpixNSWE = lumaNS + lumaWE;
    float edgeHorz1 = (-2.0 * lumaM) + lumaNS;
    float edgeVert1 = (-2.0 * lumaM) + lumaWE;
    float lumaNESE = lumaNE + lumaSE;
    float lumaNWNE = lumaNW + lumaNE;
    float edgeHorz2 = (-2.0 * lumaE) + lumaNESE;
    float edgeVert2 = (-2.0 * lumaN) + lumaNWNE;
    float lumaNWSW = lumaNW + lumaSW;
    float lumaSWSE = lumaSW + lumaSE;
    float edgeHorz4 = (abs(edgeHorz1) * 2.0) + abs(edgeHorz2);
    float edgeVert4 = (abs(edgeVert1) * 2.0) + abs(edgeVert2);
    float edgeHorz3 = (-2.0 * lumaW) + lumaNWSW;
    float edgeVert3 = (-2.0 * lumaS) + lumaSWSE;
    float edgeHorz = abs(edgeHorz3) + edgeHorz4;
    float edgeVert = abs(edgeVert3) + edgeVert4;

    float subpixNWSWNESE = lumaNWSW + lumaNESE;
    bool horzSpan = edgeHorz >= edgeVert;
    float lengthSign = horzSpan ? rcpFrame.y : rcpFrame.x;
    float subpixA = subpixNSWE * 2.0 + subpixNWSWNESE;

    // N/S become the pair across the edge (W/E for a vertical one); N is always the -axis neighbour.
    if (!horzSpan)
    {
        lumaN = lumaW;
        lumaS = lumaE;
    }
    float subpixB = (subpixA * (1.0 / 12.0)) - lumaM;

    float gradientN = lumaN - lumaM;
    float gradientS = lumaS - lumaM;
    float lumaNN = lumaN + lumaM;
    float lumaSS = lumaS + lumaM;
    bool pairN = abs(gradientN) >= abs(gradientS);
    float gradient = max(abs(gradientN), abs(gradientS));
    if (pairN)
    {
        lengthSign = -lengthSign;
    }
    float subpixC = clamp(abs(subpixB) * subpixRcpRange, 0.0, 1.0);

    // Half a texel toward the steeper neighbour, then search along the edge.
    vec2 posB = posM;
    vec2 offNP = horzSpan ? vec2(rcpFrame.x, 0.0) : vec2(0.0, rcpFrame.y);
    if (horzSpan)
    {
        posB.y += lengthSign * 0.5;
    }
    else
    {
        posB.x += lengthSign * 0.5;
    }

    if (!pairN)
    {
        lumaNN = lumaSS;
    }
    float gradientScaled = gradient * 0.25;
    float lumaMM = lumaM - lumaNN * 0.5;
    float subpixD = (-2.0 * subpixC) + 3.0;
    float subpixE = subpixC * subpixC;
    float subpixF = subpixD * subpixE;
    bool lumaMLTZero = lumaMM < 0.0;

    // End-of-edge search: sample at P0 and after each of P1..P[PS-2]; P[PS-1] is an unsampled
    // extrapolation, as in the unrolled 3.11 blocks. A side stops moving once it is done.
    int preset = int(min(pc.quality, 3u));
    int base = preset * maxSearchSteps;
    int stepCount = searchStepCount[preset];

    vec2 posN = posB - offNP * searchSteps[base];
    vec2 posP = posB + offNP * searchSteps[base];
    float lumaEndN = lumaAt(posN) - lumaNN * 0.5;
    float lumaEndP = lumaAt(posP) - lumaNN * 0.5;
    bool doneN = abs(lumaEndN) >= gradientScaled;
    bool doneP = abs(lumaEndP) >= gradientScaled;
    if (!doneN)
    {
        posN -= offNP * searchSteps[base + 1];
    }
    if (!doneP)
    {
        posP += offNP * searchSteps[base + 1];
    }

    for (int i = 2; i < stepCount; ++i)
    {
        if (doneN && doneP)
        {
            break;
        }
        if (!doneN)
        {
            lumaEndN = lumaAt(posN) - lumaNN * 0.5;
        }
        if (!doneP)
        {
            lumaEndP = lumaAt(posP) - lumaNN * 0.5;
        }
        doneN = abs(lumaEndN) >= gradientScaled;
        doneP = abs(lumaEndP) >= gradientScaled;
        if (!doneN)
        {
            posN -= offNP * searchSteps[base + i];
        }
        if (!doneP)
        {
            posP += offNP * searchSteps[base + i];
        }
    }

    float dstN = horzSpan ? (posM.x - posN.x) : (posM.y - posN.y);
    float dstP = horzSpan ? (posP.x - posM.x) : (posP.y - posM.y);
    bool goodSpanN = (lumaEndN < 0.0) != lumaMLTZero;
    bool goodSpanP = (lumaEndP < 0.0) != lumaMLTZero;
    float spanLengthRcp = 1.0 / (dstP + dstN);

    bool directionN = dstN < dstP;
    float dst = min(dstN, dstP);
    bool goodSpan = directionN ? goodSpanN : goodSpanP;
    float subpixG = subpixF * subpixF;
    float pixelOffset = (dst * (-spanLengthRcp)) + 0.5;
    float subpixH = subpixG * pc.subpixel;

    float pixelOffsetGood = goodSpan ? pixelOffset : 0.0;
    float pixelOffsetSubpix = max(pixelOffsetGood, subpixH);
    if (horzSpan)
    {
        posM.y += pixelOffsetSubpix * lengthSign;
    }
    else
    {
        posM.x += pixelOffsetSubpix * lengthSign;
    }

    outColor = vec4(textureLod(inputTexture, posM, 0.0).rgb, 1.0);
}
