#type VERTEX
#version 460 core

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inTexCoord;

layout(location = 0) out vec2 fragTexCoord;

layout(push_constant) uniform PushConstants {
    mat4 viewProj;
    mat4 modelMatrix;
    vec4 colorTint;
    vec4 uvRect;
} pc;

void main() {
    vec4 worldPos = pc.modelMatrix * vec4(inPosition, 0.0, 1.0);
    gl_Position = pc.viewProj * worldPos;
    fragTexCoord = mix(pc.uvRect.xy, pc.uvRect.zw, inTexCoord);
}

#type FRAGMENT
#version 460 core

layout(location = 0) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D uiTexture;

layout(push_constant) uniform PushConstants {
    mat4 viewProj;
    mat4 modelMatrix;
    vec4 colorTint;
    vec4 uvRect;
} pc;

void main() {
    vec4 texColor = texture(uiTexture, fragTexCoord);
    outColor = texColor * pc.colorTint;

    if (outColor.a < 0.01) {
        discard;
    }
}
