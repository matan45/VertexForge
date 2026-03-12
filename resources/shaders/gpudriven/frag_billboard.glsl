#type FRAGMENT
#version 460
#extension GL_EXT_nonuniform_qualifier : require

#include "../common/lod_crossfade.glsl"

layout(location = 0) in vec2 inUV;
layout(location = 1) in flat uint inTextureIndex;
layout(location = 2) in vec4 inColorTint;

layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) uniform sampler2D bindlessTextures[];

void main() {
    vec4 texColor = texture(bindlessTextures[nonuniformEXT(inTextureIndex)], inUV);

    if (texColor.a < 0.5) discard;

    // LOD crossfade dither (alpha stored in colorTint.a's fractional part)
    float crossfadeAlpha = fract(inColorTint.a * 256.0);
    if (crossfadeAlpha > 0.0 && ditherTest(gl_FragCoord.xy, crossfadeAlpha)) {
        discard;
    }

    vec3 finalColor = texColor.rgb * inColorTint.rgb;
    float finalAlpha = texColor.a * floor(inColorTint.a);

    outColor = vec4(finalColor, finalAlpha);
}
