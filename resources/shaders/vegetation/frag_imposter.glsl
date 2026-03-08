#type FRAGMENT
#version 460
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in vec2 inUV;
layout(location = 1) in flat uint inAtlasIndex;

layout(location = 0) out vec4 outColor;

layout(set = 4, binding = 0) uniform sampler2D bindlessTextures[];

void main() {
    vec4 texColor = texture(bindlessTextures[nonuniformEXT(inAtlasIndex)], inUV);

    if (texColor.a < 0.5) discard;

    // Simple hemisphere lighting
    vec3 lightDir = normalize(vec3(0.5, 1.0, 0.3));
    float lighting = 0.5 + 0.5 * lightDir.y;

    outColor = vec4(texColor.rgb * lighting, texColor.a);
}
