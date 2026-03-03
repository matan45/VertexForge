#type VERTEX
#version 460 core

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec4 inColor;

layout(push_constant) uniform PushConstants {
    mat4 viewProj;
} pc;

layout(location = 0) out vec4 fragColor;

void main() {
    gl_Position = pc.viewProj * vec4(inPosition, 1.0);
    fragColor = inColor;
}

#type FRAGMENT
#version 460 core

layout(location = 0) in vec4 fragColor;

layout(location = 0) out vec4 outColor;

void main() {
    outColor = fragColor;
}
