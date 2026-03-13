#type FRAGMENT
#version 460

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in float inAlpha;

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    vec4 baseColor;
    vec4 tipColor;
    float fadeStartDistance;
    float fadeEndDistance;
};

void main() {
    // Gradient from base to tip using config colors
    vec3 color = mix(baseColor.rgb, tipColor.rgb, inUV.y);

    // Simple lighting (hemisphere)
    vec3 lightDir = normalize(vec3(0.5, 1.0, 0.3));
    float NdotL = max(dot(inNormal, lightDir), 0.0);
    float ambient = 0.4;
    float diffuse = NdotL * 0.6;

    color *= (ambient + diffuse);

    if (inAlpha < 0.01) discard;

    outColor = vec4(color, inAlpha);
}
