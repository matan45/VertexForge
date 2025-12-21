#type VERTEX
#version 460 core

layout(location = 0) in vec3 inPosition;

layout(push_constant) uniform PushConstants {
    mat4 mvp;   
    vec4 color;
} pc;

void main() {
    gl_Position = pc.mvp * vec4(inPosition, 1.0);
}

#type FRAGMENT
#version 460 core

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    vec4 color;
} pc;

void main() {
    outColor = pc.color;
}
