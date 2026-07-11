#type VERTEX
#version 460 core
#extension GL_GOOGLE_include_directive : require

#include "../postprocess/fullscreen_vert.glsl"

#type FRAGMENT
#version 460 core

// VK-1490 editor selection outline composite. Dilates the visible-only
// selection mask into an ~2px silhouette ring: fragments inside a selected
// mesh discard (outside-edge only, no fill/x-ray); fragments whose 5x5 mask
// neighborhood contains a selected pixel paint the outline color.
layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D selectionMask;

layout(push_constant) uniform PushConstants {
    vec2 texelSize;    // 1.0 / mask resolution
    vec2 padding;
    vec4 outlineColor; // editor selection accent #FFA100
} pc;

void main()
{
    float center = texture(selectionMask, texCoord).r;
    if (center > 0.5) {
        discard;
    }

    float maxNeighbor = 0.0;
    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            if (x == 0 && y == 0) continue;
            vec2 offset = vec2(float(x), float(y)) * pc.texelSize;
            maxNeighbor = max(maxNeighbor, texture(selectionMask, texCoord + offset).r);
        }
    }

    if (maxNeighbor < 0.5) {
        discard;
    }
    outColor = vec4(pc.outlineColor.rgb, 1.0);
}
