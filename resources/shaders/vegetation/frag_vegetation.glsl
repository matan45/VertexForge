#type FRAGMENT
#version 460
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) flat in uint inInstanceIndex;
layout(location = 4) flat in uint inMaterialTexIndex;
layout(location = 5) flat in uint inLodLevel;

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    uint debugFlags; // bit 0: LOD color view
};

layout(set = 5, binding = 0) uniform sampler2D bindlessTextures[];

void main() {
    // LOD debug view: green=LOD0, yellow=LOD1, red=LOD2
    if ((debugFlags & 1u) != 0u) {
        vec3 lodColors[3] = vec3[3](
            vec3(0.0, 1.0, 0.0),   // LOD0 = green
            vec3(1.0, 1.0, 0.0),   // LOD1 = yellow
            vec3(1.0, 0.0, 0.0)    // LOD2 = red
        );
        uint lod = min(inLodLevel, 2u);
        outColor = vec4(lodColors[lod], 1.0);
        return;
    }

    vec3 lightDir = normalize(vec3(0.5, 1.0, 0.3));
    vec3 normal = normalize(inNormal);

    float NdotL = max(dot(normal, lightDir), 0.0);
    float ambient = 0.3;
    float lighting = ambient + (1.0 - ambient) * NdotL;

    vec3 baseColor;
    if (inMaterialTexIndex > 0u) {
        baseColor = texture(bindlessTextures[nonuniformEXT(inMaterialTexIndex)], inTexCoord).rgb;
    } else {
        baseColor = vec3(0.2, 0.5, 0.15);
    }

    outColor = vec4(baseColor * lighting, 1.0);
}
